# AGENTS.modules_draft.md — Module / Plugin Architecture (Stability-first Draft)

> Status: **Aspirational / design draft**.
>
> This document captures the intended breakdown of Animus into multiple C++ projects
> and a stable module/plugin ABI.
>
> It is a companion to:
> - `AGENTS.system_draft.md` (overall runtime blueprint)
> - `AGENTS.todo.md` (implementation milestones)
>
> Principle: prioritize **ABI stability** over short-term convenience.

---

## 1) Goals

- Keep **AgentKernel** usable as an effectively independent core.
- Allow adding/removing capabilities by dropping in/out **modules** (shared libraries).
- Avoid “same compiler / same standard library” coupling across module boundaries.
- Enable long-term cross-platform support (Linux/Windows) without redesign.

---

## 2) Recommended project split (monorepo; multiple CMake targets)

### 2.1 Core targets

1) **`animus-sdk`** (a.k.a. `animus-module-api`)
- The **stable boundary** that modules compile against.
- Contains only:
  - small, stable types
  - module interfaces
  - C ABI factory function declarations
  - version negotiation constants

2) **`animus-kernel`**
- Core runtime implementation (AgentKernel, registries, ChainRunner/ChainInstance, JobSystem, tracing, module loader).
- Links internal dependencies freely (fmt/json/sqlite/etc.) because these do **not** cross the module ABI.

3) **`animusd`**
- Host executable.
- Boots kernel, loads modules, reads config, runs main loop.

### 2.2 Module targets (shared libs)

Under `modules/<category>/<name>`:

- `modules/connectors/slack` (event source + channel sink)
- `modules/connectors/webhook`
- `modules/brokers/redis`
- `modules/brokers/rabbitmq`
- `modules/llm/mistral`
- `modules/llm/zai`
- `modules/llm/cohere`
- `modules/memory/ontology`
- `modules/memory/vector`
- `modules/memory/episodic`
- `modules/tools/terminal`
- (future) `modules/network/agent_network`

Rule of thumb:
- Kernel = orchestration + policy + registries + chain execution.
- Modules = “talks to the outside world” and/or “optional backends”.

---

## 3) ABI policy (strong stability)

### 3.1 Prefer C ABI factories

Each module exports a small set of **C** functions for discovery and lifetime management.

Example (names TBD):

- `extern "C" ANIMUS_EXPORT const animus_module_descriptor_t* animus_module_get_descriptor();`
- `extern "C" ANIMUS_EXPORT animus_module_handle_t animus_module_create(const animus_host_vtable_t* host);`
- `extern "C" ANIMUS_EXPORT void animus_module_destroy(animus_module_handle_t handle);`

The returned handle can wrap a C++ object internally, but the **boundary** is C.

### 3.2 No STL / allocator ownership across the boundary

Across the SDK boundary, avoid:
- `std::string`, `std::vector`, `std::map`
- exceptions crossing DLL boundaries
- ownership transfer of memory allocated on the other side

Instead:
- POD structs
- `const char*` + explicit length
- kernel-provided allocators (optional)
- explicit error codes + error-message retrieval functions

### 3.3 Ownership rules

Pick one convention and apply everywhere:

- **Creator frees**: the side that allocates also deallocates.
- No “kernel frees module memory” and vice versa.

### 3.4 Version negotiation

- SDK defines `ANIMUS_MODULE_API_VERSION`.
- Module descriptor declares `api_version_min` / `api_version_max` or a single `api_version`.
- Kernel refuses to load incompatible modules.

---

## 4) Module descriptor (proposal)

A module should self-describe for:
- compatibility checks
- capability listing (for registries and for prompt-time tool/channel summaries)
- auditing

Suggested descriptor fields:

- `id` (stable string, e.g. `connector.slack`)
- `name` (human-readable)
- `module_version` (semver)
- `module_api_version` (SDK ABI)
- capabilities flags:
  - provides event source?
  - provides channel sink?
  - provides tools?
  - provides LLM client?
  - provides memory module?
- optional dependencies (other module ids)
- optional config schema id / version (details TBD)

---

## 5) Kernel↔Module interaction model

### 5.1 Host interface

Kernel provides a limited host surface to modules, e.g. `animus_host_vtable_t`:
- logging
- time
- config lookup (read-only)
- registry registration calls
- job scheduling (optional, via JobSystem proxy)

Keep this surface minimal and versioned.

### 5.2 Registration

On create/init, the module registers one or more components:
- `IEventSource`
- `IChannelSink`
- `ITool`
- `ILLMClient`
- `IMemoryModule`

Kernel stores them in registries, filtered by Agent policy at runtime.

---

## 6) Build & output conventions (proposal)

Repository layout:

- `include/animus_sdk/...` (installed/public SDK headers)
- `src/sdk/...` (optional helpers)
- `src/kernel/...`
- `apps/animusd/...`
- `modules/<category>/<name>/...`
- `tests/kernel/...`
- `tests/modules/...`
- `scripts/` (build/test helpers)

Output layout (single staging dir):

- `dist/bin/animusd`
- `dist/modules/*.so|*.dll`
- `dist/config/...`

Build ergonomics:
- CMake presets for: dev / release / sanitizers
- one script that builds + runs tests for all targets

---

## 7) Security notes (early)

Even before AgentNetwork exists, module loading is a security boundary.

Baseline recommendations:
- configurable module allowlist
- module search path restrictions
- audit log: module id/version/hash loaded at boot

---

## 8) Open questions

- Exact shape of the SDK: pure C ABI vs “C ABI factories returning C++ vtables”.
- Do we want kernel-provided allocators across the boundary?
- How do we express config schema (JSON schema id? C-struct schema? custom DSL)?
- Do we sign modules (future)?
