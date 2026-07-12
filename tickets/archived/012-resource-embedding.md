# Ticket 012 — Resource Embedding (SPA into Binary)

**Priority:** P1
**Status:** Open
**Dependencies:** Ticket 009 (SPA scaffold)

## Summary

Embed the compiled SPA assets into the Animus binary so that distribution is a single file with no external web dependencies.

## Scope

- CMake script that runs `npm run build` in the UI directory and generates a C++ resource file (array of bytes)
- The generated source file is compiled into the `animus_kernel` target
- drogon serves the embedded assets from memory via a custom static file handler
- Route: `GET /` serves `index.html`; `GET /assets/*` serves JS/CSS/fonts
- SPA routing: all non-API, non-WS routes serve `index.html` (client-side routing)
- Fallback for development: config option to serve from filesystem instead of embedded resources

## Acceptance Criteria

- [ ] `cmake --build` produces a single binary with embedded UI
- [ ] Accessing `http://localhost:8080/` serves the SPA
- [ ] Client-side routing works (refreshing `/config` doesn't 404)
- [ ] Development mode can serve from filesystem for hot-reload during UI development
- [ ] Binary size increase is reasonable (< 5MB for SPA assets)

## Notes

- Use `xxd -i` style embedding or a CMake resource compiler (e.g., `cmake -P EmbedResources.cmake`)
- Gzip pre-compression of assets could reduce embedded size significantly
- This is the final integration step that makes the admin interface "just work"
