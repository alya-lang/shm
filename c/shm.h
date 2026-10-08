#ifndef ALYA_SHM_H
#define ALYA_SHM_H

/* Slot-based shared-memory ring buffer for the Alya shm package.
 * Windows: pagefile-backed file mapping + named mutex/event.
 * POSIX: shm_open segment + named semaphore (polling receiver).
 * All mappings cross the FFI boundary as small integer slots. */

int shm_open_channel(const char *name, long long capacity, int overflow, int polling_ms);
int shm_close_channel(int slot);
int shm_unlink_channel(const char *name);
long long shm_send_message(int slot, const char *data, long long msg_type, int timeout_ms);
long long shm_send_raw(int slot, const unsigned char *data, long long len, long long msg_type, int timeout_ms);
long long shm_recv_message(int slot, char *buf, long long maxlen, long long *type_out, int timeout_ms);
long long shm_peek_message(int slot, char *buf, long long maxlen, long long *type_out, int timeout_ms);
int shm_channel_stats(int slot, long long *out);
long long shm_available_bytes(int slot);
long long shm_next_message_len(int slot);
const char *shm_last_error(void);

#endif
