# Animus

Animus is a planned **agentic framework** intended as a full replacement for OpenClaw.

The goal is to provide **fine-grained control** over cognition (prompt assembly, memory policy, tool execution gates) with an architecture that is:

- **Efficient**: minimal overhead; explicit budgets.
- **Capable**: composable cognition loop and tool/hook ecosystem.
- **Secure**: auditable execution, strict I/O schemas, deterministic gates.
- **Low-noise**: explicit context selection to avoid prompt/context pollution.

## Structure (current)

This repo currently contains a minimal C++/CMake terminal app scaffold.

- `src/main.cpp` — CLI entry point
- `CMakeLists.txt` — build definition
- `dist/bin/` — build output location (local)

## Build

```bash
cmake -S . -B build
cmake --build build
./dist/bin/animus --help
```

## Status

Early scaffold only. Design will live in `DESIGN_NOTES.md` as it stabilizes.
