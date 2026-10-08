/* Shared-memory ring buffer engine for the Alya shm package.
 * Part of the Alya Language package ecosystem (https://github.com/alya-lang).
 *
 * Layout of the mapped region (all native-endian integers):
 *   [0]  magic u64, [8] version u64, [16] capacity u64,
 *   [24] write_pos u64, [32] read_pos u64, [40..63] reserved,
 *   [64..] data area of `capacity` bytes.
 * Each message on the ring is [u64 payload_len][i64 msg_type][payload].
 *
 * Overflow modes: 0 = overwrite oldest, 1 = block until space frees up.
 *
 * Return contract:
 * - open returns a slot index >= 0, or -1 on failure.
 * - send returns payload bytes written, -1 on error, -2 on timeout.
 * - recv returns payload bytes read (0 = valid empty message),
 *   -1 on error, -2 on timeout, -3 when the caller buffer is too small
 *   (nothing is consumed in that case).
 * Failure details are available via shm_last_error().
 */

#include "shm.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#endif

#define ALYA_SHM_MAX_SLOTS 64
#define ALYA_SHM_NAME_MAX 32
#define ALYA_SHM_HEADER_SIZE 64
#define ALYA_SHM_RECORD_HEADER 16
#define ALYA_SHM_MAGIC 0x004D4853594C41ULL
#define ALYA_SHM_VERSION 1ULL
#define ALYA_SHM_MIN_CAPACITY 1024ULL

typedef struct {
    int used;
    uint64_t capacity;
    int overflow;
    int polling_ms;
    unsigned char *base;
    uint64_t total_size;
#ifdef _WIN32
    HANDLE h_map;
    HANDLE h_mutex;
    HANDLE h_event;
#else
    int fd;
    sem_t *sem;
    char sem_name[64];
#endif
} alya_shm_slot;

static alya_shm_slot g_slots[ALYA_SHM_MAX_SLOTS];
static char g_last_error[256];

static void set_error(const char *msg) {
    size_t i = 0;
    if (msg == 0) {
        msg = "shm: unknown error";
    }
    while (msg[i] != '\0' && i + 1 < sizeof(g_last_error)) {
        g_last_error[i] = msg[i];
        i++;
    }
    g_last_error[i] = '\0';
}

const char *shm_last_error(void) {
    return g_last_error;
}

static int alloc_slot(void) {
    int i = 0;
    while (i < ALYA_SHM_MAX_SLOTS) {
        if (!g_slots[i].used) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].used = 1;
            return i;
        }
        i++;
    }
    set_error("shm: no free channel slots");
    return -1;
}

static int valid_slot(int slot) {
    return slot >= 0 && slot < ALYA_SHM_MAX_SLOTS && g_slots[slot].used &&
           g_slots[slot].base != 0;
}

/* Keeps [A-Za-z0-9_-], truncates to ALYA_SHM_NAME_MAX. Empty output means
 * the name carried no usable characters and the caller must fail. */
static void sanitize_name(const char *name, char *out) {
    size_t j = 0;
    size_t i = 0;
    out[0] = '\0';
    if (name == 0) {
        return;
    }
    while (name[i] != '\0' && j < ALYA_SHM_NAME_MAX) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out[j] = c;
            j++;
        }
        i++;
    }
    out[j] = '\0';
}

static uint64_t read_u64(const unsigned char *base, uint64_t off) {
    uint64_t v = 0;
    memcpy(&v, base + off, sizeof(v));
    return v;
}

static void write_u64(unsigned char *base, uint64_t off, uint64_t v) {
    memcpy(base + off, &v, sizeof(v));
}

/* Copy len bytes between the ring data area and linear memory. */
static void ring_write(unsigned char *base, uint64_t cap, uint64_t pos,
                       const void *src, uint64_t len) {
    const unsigned char *s = (const unsigned char *)src;
    uint64_t first = cap - pos;
    if (first > len) {
        first = len;
    }
    memcpy(base + ALYA_SHM_HEADER_SIZE + pos, s, (size_t)first);
    if (len > first) {
        memcpy(base + ALYA_SHM_HEADER_SIZE, s + first, (size_t)(len - first));
    }
}

static void ring_read(const unsigned char *base, uint64_t cap, uint64_t pos,
                      void *dst, uint64_t len) {
    unsigned char *d = (unsigned char *)dst;
    uint64_t first = cap - pos;
    if (first > len) {
        first = len;
    }
    memcpy(d, base + ALYA_SHM_HEADER_SIZE + pos, (size_t)first);
    if (len > first) {
        memcpy(d + first, base + ALYA_SHM_HEADER_SIZE, (size_t)(len - first));
    }
}

static uint64_t ring_used(uint64_t write_pos, uint64_t read_pos, uint64_t cap) {
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    }
    return cap - read_pos + write_pos;
}

#ifdef _WIN32

static void object_name(const char *clean, const char *suffix, char *out, size_t out_sz) {
    size_t i = 0;
    size_t j = 0;
    const char *prefix = "Local\\alya-shm-";
    while (prefix[i] != '\0' && j + 1 < out_sz) {
        out[j++] = prefix[i++];
    }
    i = 0;
    while (clean[i] != '\0' && j + 1 < out_sz) {
        out[j++] = clean[i++];
    }
    i = 0;
    while (suffix[i] != '\0' && j + 1 < out_sz) {
        out[j++] = suffix[i++];
    }
    out[j] = '\0';
}

static int lock_slot(alya_shm_slot *s, int timeout_ms) {
    DWORD w = WaitForSingleObject(s->h_mutex,
                                  timeout_ms < 0 ? 0 : (DWORD)timeout_ms);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
        return 0;
    }
    return -1;
}

static void unlock_slot(alya_shm_slot *s) {
    ReleaseMutex(s->h_mutex);
}

static void wait_for_data(alya_shm_slot *s, int slice_ms) {
    WaitForSingleObject(s->h_event, slice_ms < 0 ? 0 : (DWORD)slice_ms);
}

int shm_open_channel(const char *name, long long capacity, int overflow, int polling_ms) {
    char clean[ALYA_SHM_NAME_MAX + 1];
    char map_name[128];
    char mtx_name[128];
    char evt_name[128];
    HANDLE h_mutex;
    HANDLE h_map;
    HANDLE h_event;
    void *view = 0;
    DWORD already = 0;
    uint64_t cap = 0;
    uint64_t total = 0;
    DWORD size_lo = 0;
    DWORD size_hi = 0;
    DWORD w = 0;
    int slot = 0;
    alya_shm_slot *s = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("shm: invalid channel name");
        return -1;
    }
    if (capacity < (long long)ALYA_SHM_MIN_CAPACITY) {
        set_error("shm: capacity too small (min 1024 bytes)");
        return -1;
    }
    if (overflow != 0 && overflow != 1) {
        set_error("shm: invalid overflow mode");
        return -1;
    }
    cap = (uint64_t)capacity;
    total = cap + ALYA_SHM_HEADER_SIZE;
    size_lo = (DWORD)(total & 0xFFFFFFFFULL);
    size_hi = (DWORD)(total >> 32);
    object_name(clean, "", map_name, sizeof(map_name));
    object_name(clean, "-mtx", mtx_name, sizeof(mtx_name));
    object_name(clean, "-evt", evt_name, sizeof(evt_name));
    h_mutex = CreateMutexA(NULL, FALSE, mtx_name);
    if (h_mutex == NULL) {
        set_error("shm: CreateMutex failed");
        return -1;
    }
    w = WaitForSingleObject(h_mutex, 5000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        CloseHandle(h_mutex);
        set_error("shm: lock timeout during open");
        return -1;
    }
    h_map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                               size_hi, size_lo, map_name);
    if (h_map == NULL) {
        ReleaseMutex(h_mutex);
        CloseHandle(h_mutex);
        set_error("shm: CreateFileMapping failed");
        return -1;
    }
    already = (GetLastError() == ERROR_ALREADY_EXISTS);
    view = MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, (SIZE_T)total);
    if (view == 0) {
        CloseHandle(h_map);
        ReleaseMutex(h_mutex);
        CloseHandle(h_mutex);
        set_error("shm: MapViewOfFile failed");
        return -1;
    }
    h_event = CreateEventA(NULL, FALSE, FALSE, evt_name);
    if (h_event == NULL) {
        UnmapViewOfFile(view);
        CloseHandle(h_map);
        ReleaseMutex(h_mutex);
        CloseHandle(h_mutex);
        set_error("shm: CreateEvent failed");
        return -1;
    }
    slot = alloc_slot();
    if (slot < 0) {
        CloseHandle(h_event);
        UnmapViewOfFile(view);
        CloseHandle(h_map);
        ReleaseMutex(h_mutex);
        CloseHandle(h_mutex);
        return -1;
    }
    s = &g_slots[slot];
    s->capacity = cap;
    s->overflow = overflow;
    s->polling_ms = polling_ms <= 0 ? 10 : polling_ms;
    s->base = (unsigned char *)view;
    s->total_size = total;
    s->h_map = h_map;
    s->h_mutex = h_mutex;
    s->h_event = h_event;
    if (!already) {
        write_u64(s->base, 0, ALYA_SHM_MAGIC);
        write_u64(s->base, 8, ALYA_SHM_VERSION);
        write_u64(s->base, 16, cap);
        write_u64(s->base, 24, 0);
        write_u64(s->base, 32, 0);
    } else {
        if (read_u64(s->base, 0) != ALYA_SHM_MAGIC ||
            read_u64(s->base, 8) != ALYA_SHM_VERSION) {
            UnmapViewOfFile(view);
            CloseHandle(h_map);
            CloseHandle(h_event);
            ReleaseMutex(h_mutex);
            CloseHandle(h_mutex);
            g_slots[slot].used = 0;
            set_error("shm: existing segment is not an Alya channel");
            return -1;
        }
        if (read_u64(s->base, 16) != cap) {
            UnmapViewOfFile(view);
            CloseHandle(h_map);
            CloseHandle(h_event);
            ReleaseMutex(h_mutex);
            CloseHandle(h_mutex);
            g_slots[slot].used = 0;
            set_error("shm: capacity mismatch with existing channel");
            return -1;
        }
        s->capacity = read_u64(s->base, 16);
    }
    ReleaseMutex(h_mutex);
    return slot;
}

int shm_close_channel(int slot) {
    alya_shm_slot *s = 0;
    if (!valid_slot(slot)) {
        return 0;
    }
    s = &g_slots[slot];
    if (s->base != 0) {
        UnmapViewOfFile(s->base);
    }
    if (s->h_map != 0) {
        CloseHandle(s->h_map);
    }
    if (s->h_mutex != 0) {
        CloseHandle(s->h_mutex);
    }
    if (s->h_event != 0) {
        CloseHandle(s->h_event);
    }
    g_slots[slot].used = 0;
    return 0;
}

int shm_unlink_channel(const char *name) {
    /* Named mappings are reference-counted by the OS: once every handle
     * is closed the name vanishes, so unlink is a no-op validation. */
    char clean[ALYA_SHM_NAME_MAX + 1];
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("shm: invalid channel name");
        return -1;
    }
    return 0;
}

#else /* POSIX: shm_open segment + named semaphore */

static void sleep_slice(int ms) {
    if (ms > 0) {
        poll(0, 0, ms);
    }
}

static int lock_slot(alya_shm_slot *s, int timeout_ms) {
    long waited = 0;
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    while (sem_trywait(s->sem) != 0) {
        if (errno != EAGAIN) {
            return -1;
        }
        if (waited >= timeout_ms) {
            return -1;
        }
        sleep_slice(2);
        waited += 2;
    }
    return 0;
}

static void unlock_slot(alya_shm_slot *s) {
    sem_post(s->sem);
}

static void wait_for_data(alya_shm_slot *s, int slice_ms) {
    (void)s;
    sleep_slice(slice_ms);
}

int shm_open_channel(const char *name, long long capacity, int overflow, int polling_ms) {
    char clean[ALYA_SHM_NAME_MAX + 1];
    char shm_name[64];
    char sem_name[64];
    size_t i = 0;
    size_t j = 0;
    sem_t *sem = SEM_FAILED;
    int fd = -1;\r\n    struct stat st;
    void *view = 0;
    uint64_t cap = 0;
    int slot = 0;
    alya_shm_slot *s = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("shm: invalid channel name");
        return -1;
    }
    if (capacity < (long long)ALYA_SHM_MIN_CAPACITY) {
        set_error("shm: capacity too small (min 1024 bytes)");
        return -1;
    }
    if (overflow != 0 && overflow != 1) {
        set_error("shm: invalid overflow mode");
        return -1;
    }
    shm_name[0] = '/';
    sem_name[0] = '/';
    j = 1;
    while (clean[i] != '\0' && j + 1 < sizeof(shm_name)) {
        shm_name[j] = clean[i];
        j++;
        i++;
    }
    shm_name[j] = '\0';
    /* sem_name = shm_name + "-sem", truncated to fit. */
    i = 0;
    j = 1;
    while (clean[i] != '\0' && j + 5 < sizeof(sem_name)) {
        sem_name[j] = clean[i];
        j++;
        i++;
    }
    sem_name[j++] = '-';
    sem_name[j++] = 's';
    sem_name[j++] = 'e';
    sem_name[j++] = 'm';
    sem_name[j] = '\0';
    sem = sem_open(sem_name, O_CREAT, 0600, 1);
    if (sem == SEM_FAILED) {
        set_error("shm: sem_open failed");
        return -1;
    }
    fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        set_error("shm: shm_open failed");
        sem_close(sem);
        return -1;
    }
    if (fstat(fd, &st) != 0) {
        set_error("shm: fstat failed");
        close(fd);
        sem_close(sem);
        return -1;
    }
    cap = (uint64_t)capacity;
    /* fstat size is only a creation hint: some kernels report page-rounded
     * sizes, so anything >= expected maps cleanly and the header stays
     * authoritative for capacity checks after the lock is held. */
    if (st.st_size == 0) {
        if (ftruncate(fd, (off_t)(cap + ALYA_SHM_HEADER_SIZE)) != 0) {
            set_error("shm: ftruncate failed");
            close(fd);
            sem_close(sem);
            return -1;
        }
    } else if (st.st_size < (off_t)(cap + ALYA_SHM_HEADER_SIZE)) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "shm: existing segment smaller than requested (have %lld, want %llu; unlink first)",
                 (long long)st.st_size,
                 (unsigned long long)(cap + ALYA_SHM_HEADER_SIZE));
        close(fd);
        sem_close(sem);
        set_error(msg);
        return -1;
    }
    view = mmap(0, (size_t)(cap + ALYA_SHM_HEADER_SIZE), PROT_READ | PROT_WRITE,
                MAP_SHARED, fd, 0);
    if (view == MAP_FAILED) {
        set_error("shm: mmap failed");
        close(fd);
        sem_close(sem);
        return -1;
    }
    slot = alloc_slot();
    if (slot < 0) {
        munmap(view, (size_t)(cap + ALYA_SHM_HEADER_SIZE));
        close(fd);
        sem_close(sem);
        return -1;
    }
    s = &g_slots[slot];
    s->capacity = cap;
    s->overflow = overflow;
    s->polling_ms = polling_ms <= 0 ? 10 : polling_ms;
    s->base = (unsigned char *)view;
    s->total_size = cap + ALYA_SHM_HEADER_SIZE;
    s->fd = fd;
    s->sem = sem;
    strncpy(s->sem_name, sem_name, sizeof(s->sem_name) - 1);
    if (lock_slot(s, 5000) != 0) {
        munmap(view, (size_t)(cap + ALYA_SHM_HEADER_SIZE));
        close(fd);
        sem_close(sem);
        g_slots[slot].used = 0;
        set_error("shm: lock timeout during open");
        return -1;
    }
    /* Double-checked init: a racer may have initialized first. */
    if (read_u64(s->base, 0) == ALYA_SHM_MAGIC &&
        read_u64(s->base, 8) == ALYA_SHM_VERSION) {
        uint64_t stored = read_u64(s->base, 16);
        if (stored != cap) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "shm: capacity mismatch with existing channel (have %llu, want %llu; unlink first)",
                     (unsigned long long)stored, (unsigned long long)cap);
            unlock_slot(s);
            munmap(view, (size_t)(cap + ALYA_SHM_HEADER_SIZE));
            close(fd);
            sem_close(sem);
            g_slots[slot].used = 0;
            set_error(msg);
            return -1;
        }
        s->capacity = stored;
    } else {
        write_u64(s->base, 0, ALYA_SHM_MAGIC);
        write_u64(s->base, 8, ALYA_SHM_VERSION);
        write_u64(s->base, 16, cap);
        write_u64(s->base, 24, 0);
        write_u64(s->base, 32, 0);
    }
    unlock_slot(s);
    return slot;
}

int shm_close_channel(int slot) {
    alya_shm_slot *s = 0;
    if (!valid_slot(slot)) {
        return 0;
    }
    s = &g_slots[slot];
    if (s->base != 0) {
        munmap(s->base, (size_t)s->total_size);
    }
    if (s->fd >= 0) {
        close(s->fd);
    }
    if (s->sem != SEM_FAILED && s->sem != 0) {
        sem_close(s->sem);
    }
    g_slots[slot].used = 0;
    return 0;
}

int shm_unlink_channel(const char *name) {
    char clean[ALYA_SHM_NAME_MAX + 1];
    char shm_name[64];
    char sem_name[64];
    size_t i = 0;
    size_t j = 0;
    sanitize_name(name, clean);
    if (clean[0] == '\0') {
        set_error("shm: invalid channel name");
        return -1;
    }
    shm_name[0] = '/';
    j = 1;
    while (clean[i] != '\0' && j + 1 < sizeof(shm_name)) {
        shm_name[j] = clean[i];
        j++;
        i++;
    }
    shm_name[j] = '\0';
    sem_name[0] = '/';
    i = 0;
    j = 1;
    while (clean[i] != '\0' && j + 5 < sizeof(sem_name)) {
        sem_name[j] = clean[i];
        j++;
        i++;
    }
    sem_name[j++] = '-';
    sem_name[j++] = 's';
    sem_name[j++] = 'e';
    sem_name[j++] = 'm';
    sem_name[j] = '\0';
    shm_unlink(shm_name);
    sem_unlink(sem_name);
    return 0;
}

#endif

/* Shared send/receive over the mapped ring. Callers hold no lock. */

long long shm_send_message(int slot, const char *data, long long msg_type, int timeout_ms) {
    alya_shm_slot *s = 0;
    size_t len = 0;
    uint64_t need = 0;
    uint64_t cap = 0;
    uint64_t write_pos = 0;
    uint64_t read_pos = 0;
    uint64_t used = 0;
    uint64_t free = 0;
    long waited = 0;
    int slice = 0;
    unsigned char header[ALYA_SHM_RECORD_HEADER];
    uint64_t ulen = 0;
    int64_t mtype = 0;
    if (!valid_slot(slot)) {
        set_error("shm: invalid channel slot");
        return -1;
    }
    s = &g_slots[slot];
    if (data == 0) {
        data = "";
    }
    while (data[len] != '\0') {
        len++;
    }
    ulen = (uint64_t)len;
    mtype = (int64_t)msg_type;
    need = ulen + ALYA_SHM_RECORD_HEADER;
    cap = s->capacity;
    if (need > cap) {
        set_error("shm: message larger than channel capacity");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    slice = s->polling_ms;
    if (slice <= 0) {
        slice = 10;
    }
    while (1) {
        if (lock_slot(s, timeout_ms) != 0) {
            set_error("shm: lock timeout during send");
            return -2;
        }
        write_pos = read_u64(s->base, 24);
        read_pos = read_u64(s->base, 32);
        used = ring_used(write_pos, read_pos, cap);
        free = cap > used ? cap - used : 0;
        if (free >= need) {
            memcpy(header, &ulen, 8);
            memcpy(header + 8, &mtype, 8);
            ring_write(s->base, cap, write_pos % cap, header, ALYA_SHM_RECORD_HEADER);
            if (ulen > 0) {
                ring_write(s->base, cap, (write_pos + ALYA_SHM_RECORD_HEADER) % cap,
                           data, ulen);
            }
            write_u64(s->base, 24, (write_pos + need) % cap);
            unlock_slot(s);
#ifdef _WIN32
            SetEvent(s->h_event);
#endif
            return (long long)len;
        }
        if (s->overflow == 0) {
            /* Overwrite: drop oldest messages until the record fits. */
            while (free < need) {
                uint64_t oldest_len = 0;
                uint64_t oldest_total = 0;
                unsigned char oldest_header[ALYA_SHM_RECORD_HEADER];
                ring_read(s->base, cap, read_pos % cap, oldest_header,
                          ALYA_SHM_RECORD_HEADER);
                memcpy(&oldest_len, oldest_header, 8);
                if (oldest_len > cap) {
                    /* Corrupt header: reset the ring rather than spinning. */
                    read_pos = write_pos;
                    break;
                }
                oldest_total = oldest_len + ALYA_SHM_RECORD_HEADER;
                read_pos = (read_pos + oldest_total) % cap;
                used = ring_used(write_pos, read_pos, cap);
                free = cap > used ? cap - used : 0;
            }
            write_u64(s->base, 32, read_pos);
            memcpy(header, &ulen, 8);
            memcpy(header + 8, &mtype, 8);
            ring_write(s->base, cap, write_pos % cap, header, ALYA_SHM_RECORD_HEADER);
            if (ulen > 0) {
                ring_write(s->base, cap, (write_pos + ALYA_SHM_RECORD_HEADER) % cap,
                           data, ulen);
            }
            write_u64(s->base, 24, (write_pos + need) % cap);
            unlock_slot(s);
#ifdef _WIN32
            SetEvent(s->h_event);
#endif
            return (long long)len;
        }
        unlock_slot(s);
        if (waited >= timeout_ms) {
            set_error("shm: send timeout (channel full)");
            return -2;
        }
        wait_for_data(s, slice);
        waited += slice;
    }
}

long long shm_recv_message(int slot, char *buf, long long maxlen, long long *type_out,
                           int timeout_ms) {
    alya_shm_slot *s = 0;
    long waited = 0;
    int slice = 0;
    if (!valid_slot(slot)) {
        set_error("shm: invalid channel slot");
        return -1;
    }
    s = &g_slots[slot];
    if (buf == 0 || maxlen < 0) {
        set_error("shm: invalid receive buffer");
        return -1;
    }
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    slice = s->polling_ms;
    if (slice <= 0) {
        slice = 10;
    }
    while (1) {
        uint64_t cap = s->capacity;
        uint64_t write_pos = 0;
        uint64_t read_pos = 0;
        uint64_t used = 0;
        unsigned char header[ALYA_SHM_RECORD_HEADER];
        uint64_t ulen = 0;
        int64_t mtype = 0;
        if (lock_slot(s, timeout_ms) != 0) {
            set_error("shm: lock timeout during receive");
            return -2;
        }
        write_pos = read_u64(s->base, 24);
        read_pos = read_u64(s->base, 32);
        used = ring_used(write_pos, read_pos, cap);
        if (used >= ALYA_SHM_RECORD_HEADER) {
            ring_read(s->base, cap, read_pos % cap, header, ALYA_SHM_RECORD_HEADER);
            memcpy(&ulen, header, 8);
            memcpy(&mtype, header + 8, 8);
            if (ulen + ALYA_SHM_RECORD_HEADER <= used && ulen <= cap) {
                if ((long long)ulen > maxlen) {
                    unlock_slot(s);
                    return -3;
                }
                if (ulen > 0) {
                    ring_read(s->base, cap,
                              (read_pos + ALYA_SHM_RECORD_HEADER) % cap,
                              buf, ulen);
                }
                write_u64(s->base, 32, (read_pos + ulen + ALYA_SHM_RECORD_HEADER) % cap);
                unlock_slot(s);
                if (type_out != 0) {
                    *type_out = (long long)mtype;
                }
                return (long long)ulen;
            }
        }
        unlock_slot(s);
        if (waited >= timeout_ms) {
            return -2;
        }
        wait_for_data(s, slice);
        waited += slice;
    }
}

long long shm_available_bytes(int slot) {
    alya_shm_slot *s = 0;
    uint64_t write_pos = 0;
    uint64_t read_pos = 0;
    long long used = 0;
    if (!valid_slot(slot)) {
        return -1;
    }
    s = &g_slots[slot];
    if (lock_slot(s, 1000) != 0) {
        return -1;
    }
    write_pos = read_u64(s->base, 24);
    read_pos = read_u64(s->base, 32);
    used = (long long)ring_used(write_pos, read_pos, s->capacity);
    unlock_slot(s);
    return used;
}

long long shm_next_message_len(int slot) {
    alya_shm_slot *s = 0;
    uint64_t write_pos = 0;
    uint64_t read_pos = 0;
    uint64_t used = 0;
    unsigned char header[ALYA_SHM_RECORD_HEADER];
    uint64_t ulen = 0;
    if (!valid_slot(slot)) {
        return -1;
    }
    s = &g_slots[slot];
    if (lock_slot(s, 1000) != 0) {
        return -1;
    }
    write_pos = read_u64(s->base, 24);
    read_pos = read_u64(s->base, 32);
    used = ring_used(write_pos, read_pos, s->capacity);
    if (used >= ALYA_SHM_RECORD_HEADER) {
        ring_read(s->base, s->capacity, read_pos % s->capacity, header,
                  ALYA_SHM_RECORD_HEADER);
        memcpy(&ulen, header, 8);
        if (ulen + ALYA_SHM_RECORD_HEADER <= used && ulen <= s->capacity) {
            unlock_slot(s);
            return (long long)ulen;
        }
    }
    unlock_slot(s);
    return -2;
}
