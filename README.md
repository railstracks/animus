# Animus

> [!IMPORTANT]
> Before switching this repository to **public** (if ever), remove **market research / positioning** files and **`CONCEPT.md`** first.

Animus is a planned **agentic framework** intended as a full replacement for OpenClaw.

The goal is to provide **fine-grained control** over cognition (prompt assembly, memory policy, tool execution gates) with an architecture that is:

- **Efficient**: minimal overhead; explicit budgets.
- **Capable**: composable cognition loop and tool/hook ecosystem.
- **Secure**: auditable execution, strict I/O schemas, deterministic gates.
- **Low-noise**: explicit context selection to avoid prompt/context pollution.

## Structure (current)

This repo currently contains a minimal C++/CMake scaffold with:

- a small job system library
- a stability-first **module ABI** (`animus-sdk` headers)
- a minimal **module loader**
- a hello-world example module

Key paths:

- `include/animus_sdk/` — stable module ABI headers
- `src/core/` — core components (currently: JobSystem)
- `src/kernel/module/` — module loader
- `modules/example/hello/` — example shared library module
- `dist/bin/` — build output location (local)
- `dist/modules/` — module output location (local)

## Build

```bash
cmake -S . -B build
cmake --build build
./dist/bin/animusd --help

# Smoke tests
./dist/bin/animusd --test-jobs
./dist/bin/animusd --load-hello-module
```

## Status

Early scaffold only. Design will live in `DESIGN_NOTES.md` as it stabilizes.
