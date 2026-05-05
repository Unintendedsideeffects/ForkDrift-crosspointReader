# Getting Started

This guide helps you build and run CrossPoint locally.

## Prerequisites

- **uv**: Used to install and run the pinned Python and PlatformIO toolchain.
- **PlatformIO**: VS Code + PlatformIO IDE is optional for editor integration.
- **Microcontroller**: Target hardware is **ESP32-C3** with **C++20** support.
- **Python**: Python 3.11+.
- **Clang-format**: `clang-format` 21+ in your `PATH` (CI uses clang-format 21).
- **USB-C cable**: For flashing and serial monitoring.
- **Hardware**: Xteink X4 device for physical testing.

### Installing clang-format 21

If `./bin/clang-format-fix` fails with version errors, install clang-format 21:

```sh
# Debian/Ubuntu (try this first)
sudo apt-get update && sudo apt-get install -y clang-format-21

# macOS (Homebrew)
brew install clang-format
```

Verify version: `clang-format-21 --version`. The reported major version must be 21 or newer.

## Clone and Initialize

Clone the repository and its **submodules** (important!) using the **`fork-drift`** branch:

```sh
git clone --recursive https://github.com/Unintendedsideeffects/ForkDrift-crosspointReader --branch fork-drift
cd ForkDrift-crosspointReader
```

If you already cloned without submodules or are on a different branch:

```sh
git checkout fork-drift
git submodule update --init --recursive
```

Enable the repository-managed Git hooks (required once per clone):

```sh
git config core.hooksPath scripts/hooks
```

Install the pinned local toolchain before building:

```sh
uv sync --frozen
```

## Build

```sh
uv run pio run
```

### Flash

```sh
uv run pio run --target upload
```

## First checks before opening a PR

Before submitting any changes, ensure your code passes these local checks:

```sh
uv run ./bin/clang-format-fix
uv run pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
bash test/run_host_tests.sh
python3 scripts/validate_contract_server.py
uv run pio run
```

## What to read next

- [Architecture Overview](./architecture.md)
- [Development Workflow](./development-workflow.md)
- [Testing and Debugging](./testing-debugging.md)
