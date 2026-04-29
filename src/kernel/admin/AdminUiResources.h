# Ticket 012 Report — Resource Embedding (SPA into Binary)

## Summary
Ticket `012-resource-embedding` has been implemented with build-time SPA asset embedding and runtime in-memory serving through the admin server.

## Implemented Scope
- Added CMake-controlled embedding pipeline:
  - `ANIMUS_ADMIN_UI_EMBED` (default `ON`)
  - `ANIMUS_ADMIN_UI_BUILD` (default `ON`)
- Added build generator script:
  - `cmake/GenerateEmbeddedAdminUi.cmake`
  - Uses `xxd` to convert each built SPA file into C++ byte arrays.
- Added embedded resource interface + fallback stub:
  - `src/kernel/admin/AdminUiResources.h`
  - `src/kernel/admin/AdminUiResourcesStub.cpp`
- Updated `CMakeLists.txt` to:
  - Build `admin-ui` (`npm install && npm run build`) when embedding is enabled.
  - Generate `build/generated/AdminUiEmbeddedResources.cpp`.
  - Compile generated resources into `animus_kernel`.
- Updated `AdminServer` static route handlers to serve embedded assets from memory:
  - `GET /` serves embedded `index.html`.
  - `GET /assets/*` and `GET /favicon.ico` serve embedded files with content-type mapping.
  - SPA route fallback behavior remains intact (`/chat`, `/memory`, `/config`, `/interfaces`, `/constitution`, `/logs` -> `index.html`).
- Added development override for filesystem serving:
  - `ANIMUS_ADMIN_UI_SERVE_FILESYSTEM=1`
  - When set, server bypasses embedded assets and serves from `admin-ui/dist` / configured filesystem candidates.

## Validation Against Acceptance Criteria
- `cmake --build` produces an executable with embedded SPA resources compiled into runtime library: ✅
- Accessing `http://localhost:8080/` serves SPA through admin server routes: ✅
- Client-side routing fallback remains active for non-API SPA paths: ✅
- Development mode can serve filesystem assets via env override: ✅
- Binary size increase reasonable (`< 5MB` target): ⚠️ Not fully met in current form.
  - Built SPA asset directory is ~`5.1MB`.
  - Generated embed source is ~`32MB` text during build.
  - `build/libanimus_kernel.so` is ~`9.5MB` after embedding.

## Notes
- In this build setup, assets are embedded in `animus_kernel` (shared library), not directly in `dist/bin/animusd`.
- The old filesystem serving path remains available as a deliberate development fallback.
- Content-type handling for embedded assets was corrected to avoid duplicate `Content-Type` headers.

## Verification
- `cd admin-ui && npx tsc --noEmit` passes.
- `cd admin-ui && npm run build` passes.
- `cmake --build build -j6` passes with embedded resource generation.
- `ctest --test-dir build --output-on-failure` passes (`6/6`).