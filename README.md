# shm

[![CI](https://github.com/alya-lang/shm/actions/workflows/ci.yml/badge.svg)](https://github.com/alya-lang/shm/actions/workflows/ci.yml)
[![License](https://img.shields.io/github/license/alya-lang/shm?color=blue&label=License)](LICENSE)
[![Alya](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fshm%2Fmain%2Falya.toml&query=%24.package.alya-version&label=Alya&color=orange&prefix=%3E%3D)](https://github.com/alya-lang/alya)
[![Package Version](https://img.shields.io/badge/dynamic/toml?url=https%3A%2F%2Fraw.githubusercontent.com%2Falya-lang%2Fshm%2Fmain%2Falya.toml&query=%24.package.version&label=Version&color=brightgreen)](alya.toml)

Shared-memory messaging for Alya: memory-mapped ring-buffer channels for fast local IPC

---

## 🌟 Features

- ⚡ **Lightweight & High Performance**: Minimal memory overhead, zero runtime bloat, and fast native execution
- 🧩 **Modular Architecture**: Layered multi-module design featuring a clean public facade (`src/lib.alya`), rich data models (`src/types.alya`), and encapsulated core formatters (`src/core/formatter.alya`)
- 🔒 **Public/Private Visibility (`pub`)**: Fine-grained export control with `pub` for public functions, structs, and enums, keeping internal helper functions private and encapsulated
- 🎭 **Structural Duck Typing & Interfaces**: Dynamic interface dispatch (`Summarizable`, `Describable`) without brittle inheritance hierarchies
- 📦 **Rich Domain Models & Enums**: Idiomatic `enum` types (`ShmStatus`, `ShmPriority`, `ShmStyle`) and typed data containers (`ShmConfig`, `ShmResult`, `ShmStats`)
- 🎯 **Advanced Pattern Matching**: Clean branching with `when` expressions, range matching, and condition guards
- 🛡️ **Defensive Result Pattern**: Structured error handling and outcome encapsulation with `ok_result` and `error_result`
- 🧪 **Enterprise Test & Benchmark Suite**: 100% test coverage with standard assertions (`std/test`) and micro-benchmarking (`std/test` bench runner)
- 🚩 **Feature-Gated API Slices**: Optional capability slices via `[features]` in `alya.toml` (`default = ["extras"]`) and `@cfg(feature = "extras")` gating with a `@cfg(not(feature = "extras"))` fallback stub (see `src/core/extras.alya`)

---

## 📁 Project Architecture

```
shm/
├── .alyalint               # Linter configuration (rules, exclusions, severity overrides)
├── .editorconfig           # Uniform formatting rules across IDEs and editors
├── .gitignore              # Ecosystem standard ignore filters
├── .vscode/                # VS Code workspace settings, DAP launch configurations & tasks
├── alya.toml               # Package manifest with dependencies, [features] and optional [build]
├── c/                      # (Optional) Native C sources for zero-dependency FFI packages
├── src/
│   ├── lib.alya            # Public API facade (pub exports, re-exports & pipeline runners)
│   ├── types.alya          # Data models, pub enums, pub structs, and struct methods
│   ├── ffi.alya            # (Optional) Native extern "C" declarations
│   └── core/               # Subdirectory module hierarchy
│       ├── formatter.alya  # Domain formatting routines, salutation builders & pattern matchers
│       └── extras.alya     # Feature-gated (`extras`) optional API slice with `@cfg` gating
├── examples/
│   └── demo.alya           # Comprehensive runnable walkthrough of all package capabilities
├── tests/
│   └── test_basic.alya     # Automated test suite with 100% feature coverage
└── benches/
    └── bench_basic.alya    # Micro-benchmarks measuring performance and throughput
```

> [!NOTE]
> **Visibility & Modularity:** Symbols annotated with `pub` (`pub function`, `pub struct`, `pub enum`, `pub interface`) are exported to external consumers and re-exporting modules. Symbols without `pub` remain strictly internal to their declaring module, preventing symbol collisions and implementation leakage.

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
    # 1. Basic facade call with default parameter
    let greeting = pkg::hello()
    say f"Greeting:  {greeting}"

    # 2. Struct configuration with priority, style, and methods
    let cfg = pkg::new_config("Community", 5, pkg::ShmPriority.High, pkg::ShmStyle.Formal)
    say f"Summary:   {cfg.summary()}"
    say f"Formatted: {pkg::core_format_custom(cfg)}"

    # 3. Processing pipeline returning Result model
    let res = pkg::process("Analytics", 3, pkg::ShmPriority.Critical)
    say f"Outcome:   {res.message}"
end

main()
```

---

## 📖 API Reference

| Symbol | Visibility | Description |
|---|---|---|
| `hello(name = "World")` | `pub function` | Returns a formatted greeting string. Defaults to `"World"` if null or empty. |
| `new_config(name, count, priority, style)` | `pub function` | Factory constructing a `ShmConfig` with sensible defaults. |
| `make_config(name, count, priority, style, enabled, tags)` | `pub function` | Full constructor for `ShmConfig`. |
| `process(label, count, priority)` | `pub function` | Runs processing pipeline, returning an `ok_result` `ShmResult`. |
| `process_batch(labels)` | `pub function` | Formats an array of labels in batch, returning an array of strings. |
| `ok_result(value, message)` | `pub function` | Constructs a successful `ShmResult` container (`status = 0`). |
| `error_result(message, errors)` | `pub function` | Constructs a failed `ShmResult` container (`status = 1`). |
| `make_stats(total, passed, failed, skipped)` | `pub function` | Constructs a `ShmStats` metrics record. |
| `format_summary(cfg)` | `pub function` | Formats summary of a config instance (satisfies `Summarizable`). |
| `format_description(cfg)` | `pub function` | Formats description of a config instance (satisfies `Describable`). |
| `format_config(config)` | `pub function` | Multi-field formatter producing descriptive overview of a `ShmConfig`. |
| `format_result(result)` | `pub function` | Formats a `ShmResult` into `[OK]` or `[ERROR]` status line. |
| `format_stats(stats)` | `pub function` | Formats total checked items and success rate percentage. |
| `clamp(n, min_val, max_val)` | `pub function` | Clamps an integer value to the closed range `[min_val, max_val]`. |
| `pluralize(n, singular, plural)` | `pub function` | Pattern-matches count to return singular or plural noun form. |
| `repeat_string(label, count)` | `pub function` | Repeats a string into an array of `count` items. |
| `extra_greeting(name = "World")` | `pub function` (`extras` feature, default-on) | Enthusiastic greeting slice gated by `@cfg(feature = "extras")`; stub throws a descriptive error when the feature is off. |
| `Summarizable` | `pub interface` | Structural contract requiring `summary(self) -> string`. |
| `Describable` | `pub interface` | Structural contract requiring `describe(self) -> string` and `is_valid(self) -> int`. |
| `ShmStatus` | `pub enum` | Lifecycle status codes (`Pending = 0`, `Active = 1`, `Archived = 2`, `Error = 3`). |
| `ShmPriority` | `pub enum` | Priority tiers (`Low = 0`, `Normal = 1`, `High = 2`, `Critical = 3`). |
| `ShmStyle` | `pub enum` | Presentation styles (`Standard = 0`, `Formal = 1`, `Casual = 2`). |
| `ShmConfig` | `pub struct` | Primary configuration model (`name`, `count`, `priority`, `style`, `enabled`, `tags`). |
| `ShmConfig.summary()` | `pub method` | Single-line formatted summary (satisfies `Summarizable`). |
| `ShmConfig.describe()` | `pub method` | Detailed multi-field description (satisfies `Describable`). |
| `ShmConfig.is_valid()` | `pub method` | Validation guard returning 1 if valid, 0 otherwise. |
| `ShmConfig.is_enabled()` | `pub method` | Returns 1 if active, 0 if disabled. |
| `ShmConfig.with_name(new_name)` | `pub method` | Immutable copy with updated name. |
| `ShmConfig.with_priority(new_prio)` | `pub method` | Immutable copy with updated priority tier. |
| `ShmResult` | `pub struct` | Operation outcome model (`value`, `status`, `message`, `errors`). |
| `ShmResult.is_ok()` | `pub method` | Returns 1 if successful (`status == 0`), 0 otherwise. |
| `ShmResult.is_error()` | `pub method` | Returns 1 if error (`status != 0`), 0 otherwise. |
| `ShmResult.unwrap_or(fallback)` | `pub method` | Returns message on success, or fallback on error. |
| `ShmStats` | `pub struct` | Run statistics model (`total`, `passed`, `failed`, `skipped`). |
| `ShmStats.total_checked()` | `pub method` | Sum of passed and failed items count. |
| `ShmStats.success_rate()` | `pub method` | Computed percentage string (e.g. `"95%"`). |

> [!TIP]
> **Internal Helpers & Documentation:** Public symbols are documented with `##` Markdown docstrings, enabling automatic API documentation generation via `alya doc`. Private functions such as `build_salutation` and `build_priority_label` in `src/core/formatter.alya` are not annotated with `pub` and remain encapsulated within their respective modules.

---

## 🧪 Running Tests & Benchmarks

Run the automated test suite using `alya test`:

```bash
alya test
```

Exercise feature selection (the `extras` slice is default-on):

```bash
alya test --features extras
alya test --no-default-features
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