# shm

[![CI](https://github.com/alya-lang/shm/actions/workflows/ci.yml/badge.svg)](https://github.com/alya-lang/shm/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/alya-lang/shm?color=blue&label=License)](LICENSE)
[![Alya](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fshm%2Fmain%2Falya.toml&query=%24.package.alya-version&label=Alya&color=orange&prefix=%3E%3D)](https://github.com/alya-lang/alya)
[![Package Version](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fshm%2Fmain%2Falya.toml&query=%24.package.version&label=Version&color=brightgreen)](alya.toml)

Shared-memory messaging for Alya: memory-mapped ring-buffer channels for fast local IPC

---

## 🌟 Features

- ⚡ **Zero-Copy Local Messaging**: Memory-mapped ring buffer shared between processes — no sockets, no kernel copies on the hot path (~3µs roundtrips)
- 🔌 **Native Cross-Platform Engine**: Bundled zero-dependency C engine (`c/shm.c`) — Windows file mappings with named mutex/event, POSIX `shm_open` with named semaphores
- 🔒 **True Cross-Process Locking**: Semaphore/mutex-guarded positions on every platform (no local-only locks)
- 🧩 **Binary-Framed Records**: `[len][type][payload]` records with wraparound handling; UTF-8 text plus binary byte arrays (NUL-safe)
- 🎯 **Overflow Policies**: `Overwrite` drops oldest messages under pressure, `Block` waits for the reader with a timeout
- 🔭 **Peek, Try-Ops & Stats**: Non-consuming reads, non-blocking variants, and shared lifetime counters (`sent`/`received`/`dropped`)
- 🤝 **Request/Reply Helper**: Two-channel call pattern with correlation types for service-style messaging
- 📢 **Broadcast Subscribers**: Up to 7 independent readers per channel — every subscriber observes every message, slow readers apply backpressure
- 🛡️ **Defensive Result Pattern**: Structured `ShmError` throws for bad names, capacity mismatches, oversize messages, and timeouts
- 🧪 **Enterprise Test & Benchmark Suite**: Dual-handle roundtrips, overwrite math, block timeouts, and mismatch coverage with standard assertions (`std/test`)

---

## 📁 Project Architecture

```
shm/
├── .alyalint               # Linter configuration (rules, exclusions, severity overrides)
├── .editorconfig           # Uniform formatting rules across IDEs and editors
├── .gitignore              # Ecosystem standard ignore filters
├── .vscode/                # VS Code workspace settings, DAP launch configurations & tasks
├── alya.toml               # Package manifest with dependencies and native [build]
├── c/                      # Bundled native ring-buffer engine
│   ├── shm.c               # Ring buffer, locks, events, send/recv/peek paths
│   └── shm.h               # Native API header
├── src/
│   ├── lib.alya            # Public API facade (lifecycle and messaging)
│   ├── types.alya          # Data models, pub enums, pub structs, and struct methods
│   ├── ffi.alya            # Native extern "C" declarations for the engine
│   └── core/               # Subdirectory module hierarchy
│       └── channel.alya    # Open/send/recv/close/unlink wrappers
├── examples/
│   └── demo.alya           # Runnable walkthrough of echo and overwrite
├── tests/
│   └── test_basic.alya     # Automated test suite with ring-behavior coverage
└── benches/
    └── bench_basic.alya    # Micro-benchmarks for config and roundtrips
```

> [!NOTE]
> **Visibility & Modularity:** Symbols annotated with `pub` (`pub function`, `pub struct`, `pub enum`) are exported to external consumers and re-exporting modules. Symbols without `pub` remain strictly internal to their declaring module, preventing symbol collisions and implementation leakage.

---

## 📦 Installation

Add `shm` to the `[dependencies]` section in your `alya.toml`:

```toml
[dependencies]
shm = { git = "https://github.com/alya-lang/shm", branch = "main" }
```

Or install it directly using the Alya package CLI:

```bash
alya add shm --git https://github.com/alya-lang/shm --branch main
alya install
```

---

## 🚀 Quick Start

```alya
import "shm" as pkg

function main()
    # Two handles on one name share a segment (threads, or two processes
    # opening the same name with the same capacity).
    pkg::unlink_channel("orders")

    let writer = pkg::open_channel("orders")
    let reader = pkg::open_channel("orders")

    pkg::send_text(writer, "hello", 1)
    let text, kind = pkg::recv_text(reader, 2000)
    say f"Got: {text} (type={kind})"

    # Broadcast: a second reader observes the same history.
    let sub = pkg::subscribe(reader)
    pkg::send_text(writer, "next", 2)
    let seen, seen_kind = pkg::recv_for(reader, sub, 2000)
    say f"Sub: {seen} (type={seen_kind})"

    pkg::close_channel(writer)
    pkg::close_channel(reader)
    pkg::unlink_channel("orders")
end

main()
```

---

## 📖 API Reference

| Symbol | Visibility | Description |
|---|---|---|
| `open_channel(name, capacity, overflow)` | `pub function` | Opens (or joins) a channel; throws `ShmError` on bad config or mismatch. |
| `open_config(cfg)` | `pub function` | Opens a channel from a `ShmConfig` record. |
| `default_config(name, capacity, overflow)` | `pub function` | Creates a `ShmConfig` with sensible defaults. |
| `close_channel(slot)` | `pub function` | Closes a slot (ignores invalid handles). |
| `unlink_channel(name)` | `pub function` | Removes a stale segment (needed on POSIX after crashes). |
| `send_text(slot, payload, msg_type)` | `pub function` | Sends one message; returns payload bytes written. |
| `recv_text(slot, timeout_ms)` | `pub function` | Receives next message; `(payload, type)` with null payload on timeout. |
| `peek_text(slot, timeout_ms)` | `pub function` | Reads next message without consuming; null payload on timeout. |
| `try_send_text(slot, payload, msg_type)` | `pub function` | Non-blocking send; bytes written or -1. |
| `try_recv_text(slot)` | `pub function` | Non-blocking receive; null payload when empty. |
| `send_bytes(slot, bytes, msg_type)` | `pub function` | Sends binary payload (NUL-safe); returns bytes written. |
| `recv_bytes(slot, timeout_ms)` | `pub function` | Receives binary payload; `(bytes, type)`, null array on timeout. |
| `call_text(req_slot, rep_slot, payload, msg_type, timeout_ms)` | `pub function` | Request/reply over a channel pair; `(reply, type)`. |
| `subscribe(slot)` | `pub function` | Subscribes a broadcast reader; returns id (1-7). |
| `unsubscribe(slot, reader)` | `pub function` | Removes a broadcast reader. |
| `recv_for(slot, reader, timeout_ms)` | `pub function` | Receives for a reader; null payload on timeout. |
| `peek_for(slot, reader, timeout_ms)` | `pub function` | Peeks for a reader without consuming. |
| `channel_stats(slot)` | `pub function` | Reads shared `ShmStats` counters. |
| `pending_bytes(slot)` | `pub function` | Buffered byte count, or -1 for invalid handles. |
| `shm_open(name, capacity, overflow, polling_ms)` | `pub function` | Core open with explicit poll slice. |
| `shm_open_config(cfg)` | `pub function` | Core open from a validated config. |
| `shm_close(slot)` | `pub function` | Core close. |
| `shm_unlink(name)` | `pub function` | Core unlink. |
| `shm_send(slot, payload, msg_type, timeout_ms)` | `pub function` | Core send with explicit timeout. |
| `shm_recv(slot, max_bytes, timeout_ms)` | `pub function` | Core receive with growing buffer. |
| `shm_peek(slot, max_bytes, timeout_ms)` | `pub function` | Core peek without consuming. |
| `shm_try_send(slot, payload, msg_type)` | `pub function` | Core non-blocking send. |
| `shm_try_recv(slot, max_bytes)` | `pub function` | Core non-blocking receive. |
| `shm_send_bytes(slot, bytes, msg_type, timeout_ms)` | `pub function` | Core binary send with explicit timeout. |
| `shm_recv_bytes(slot, max_bytes, timeout_ms)` | `pub function` | Core binary receive with growing buffer. |
| `shm_stats(slot)` | `pub function` | Core counter read returning `ShmStats`. |
| `shm_subscribe(slot)` | `pub function` | Core subscribe returning reader id. |
| `shm_unsubscribe(slot, reader)` | `pub function` | Core unsubscribe. |
| `shm_recv_for(slot, reader, max_bytes, timeout_ms)` | `pub function` | Core reader receive. |
| `shm_peek_for(slot, reader, max_bytes, timeout_ms)` | `pub function` | Core reader peek. |
| `shm_reader_available(slot, reader)` | `pub function` | Buffered bytes for one reader. |
| `shm_available(slot)` | `pub function` | Core buffered-bytes probe. |
| `shm_config(name, capacity, overflow, polling_ms)` | `pub function` | Full config constructor. |
| `shm_config_is_valid(cfg)` | `pub function` | True for usable configurations. |
| `ShmOverflow` | `pub enum` | Overflow policy (`Overwrite = 0`, `Block = 1`). |
| `ShmConfig` | `pub struct` | Channel configuration (`name`, `capacity`, `overflow`, `polling_ms`). |
| `ShmConfig.is_valid()` | `pub method` | True for usable configurations. |
| `ShmConfig.summary()` | `pub method` | `"name (capacity bytes, overflow=N)"` summary. |
| `ShmError` | `pub struct` | Thrown failure (`message`, `code`). |
| `ShmStats` | `pub struct` | Lifetime counters (`sent`, `received`, `dropped`). |
| `ShmStats.accounted()` | `pub method` | Sum of received and dropped messages. |
| `ShmStats.summary()` | `pub method` | `"sent=N received=N dropped=N"` summary. |

> [!TIP]
> **Fan-Out:** One channel feeds the legacy default stream plus up to 7 subscribers, and every reader observes every message. A lagging reader pins the writer (backpressure in both overflow modes); under `Overwrite` pressure it misses dropped messages instead. Windows wakes receivers via events; POSIX receivers poll — tune `polling_ms` for your latency/CPU trade-off.

---

## 🧪 Running Tests & Benchmarks

Run the automated test suite using `alya test`:

```bash
alya test
```

Generate static API documentation:

```bash
alya doc . -o docs --markdown
```

Run the benchmark suite:

```bash
alya run benches/bench_basic.alya
```

Run the example demo:

```bash
alya run examples/demo.alya
```

Check code formatting:

```bash
alya fmt . --check
```

Run static code linter:

```bash
alya lint . --check
```

---

### 💻 Developer Tooling & VS Code Integration

This package comes preconfigured with recommended workspace settings and tasks for **Visual Studio Code**:
- **LSP & Formatting**: Auto-formatting on save and real-time Language Server diagnostics via `alya-lang.vscode-alya`.
- **DAP Debugging**: Launch configurations in `.vscode/launch.json` ready for interactive step-debugging via `F5`.
- **Predefined Tasks**: Press `Ctrl+Shift+B` or run tasks (`Test`, `Lint`, `Format`, `Build Docs`) directly from the Command Palette.

---

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository and clone it locally
2. Install dependencies:
   ```bash
   alya install
   ```
3. Create your feature branch (`git checkout -b feature/my-feature`)
4. Verify tests and formatting before opening a PR:
   ```bash
   alya test
   ```
5. Commit your changes (`git commit -m "feat: add feature"`) and open a Pull Request

---

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
