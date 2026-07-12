# Ticket 020 Report: Provider Concurrency Throttle

**Status:** ✅ Complete
**Commit:** 10873ac

## What was done

### ProviderThrottle (new class)
- Per-provider concurrency limiter using mutex + condition variable
- `Acquire(providerId)` — returns a `SlotGuard` (RAII). Blocks if at capacity, FIFO queue for waiters
- Dynamic concurrency lookup — reads from `ProviderState` on each acquire, so runtime config changes take effect without restart
- `concurrency = 0` → unlimited (no throttling, immediate return)
- `concurrency = 1` → serial (one call at a time, default)
- `concurrency = N` → up to N parallel calls

### SlotGuard (RAII)
- Auto-releases the provider slot on destruction (scope exit)
- Move-constructible and move-assignable for use with late initialization
- Safe across error paths, exceptions, and early returns

### Integration
- `ProviderThrottle` created in `AgentKernel::Start()`, wired to `AdminServer`
- Concurrency lookup captures `this` + reads `m_providersByName` under `m_providerMutex`
- Chat handler (Cognition lane jobs) acquires slot before chain execution
- Slot released when job lambda exits (RAII guard goes out of scope)

### Persistence
- `concurrency` field added to `ProviderState` (int, default: 1)
- Serialized in `providers.json` alongside other provider properties
- Loaded from disk on startup via `LoadProvidersFromDisk`
- Preserved through UI edits via `ProviderFromJson`/`BuildProviderJson`

### Admin UI
- Number input field in provider edit form
- Min: 0, hint: "0 = unlimited"
- i18n: English ("Max Concurrency") and Dutch ("Max. gelijktijdige verzoeken")

## Acceptance Criteria

- [x] `ProviderThrottle` with `Acquire` / `SlotGuard` RAII implemented
- [x] `concurrency` field persisted in `providers.json`
- [x] Admin UI exposes concurrency in provider edit form
- [x] Concurrency = 0 means unlimited (no throttling)
- [x] Concurrency = 1 serializes all calls to that provider
- [x] Concurrency = N allows up to N parallel calls, excess queued FIFO
- [x] Slots released on success, error, and exception paths (RAII)
- [x] Queued jobs wake promptly when a slot frees (condition variable)
- [x] Multiple providers throttled independently
- [x] Existing chat flow works unchanged (default concurrency = 1)
- [x] Full build + tests pass (5/6, 1 pre-existing failure)

## Files changed
- **New:** `ProviderThrottle.h`, `ProviderThrottle.cpp`
- **Modified:** `AdminServer.h` (concurrency field + throttle pointer), `AdminServer.cpp` (throttle acquire in chat handler, serialization), `AgentKernel.h` (throttle member), `AgentKernel.cpp` (throttle creation + wiring), `CMakeLists.txt` (new source), `ProvidersView.vue` (form field), `en.ts`, `nl.ts` (i18n)

## Design decisions
- Throttle is **separate from `ILLMProvider`** — providers stay pure HTTP+parse, throttling is infrastructure
- Lookup function reads config dynamically — no restart needed for concurrency changes
- RAII guard over manual acquire/release — eliminates a class of leaks
- `SlotPool` is lazily created and auto-cleaned when empty — no upfront configuration
