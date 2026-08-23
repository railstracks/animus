# Standing contributor lane: API package

Pick a service you already use and add a thin typed client package under this repository's API layout.

## Steps

1. Open an issue naming the target service and auth mode.
2. Scaffold `packages/<service>-api` (or the repo's equivalent path).
3. Implement health/ping + one read endpoint with tests.
4. Document env vars and a minimal example in the package README.

## Acceptance

- Package builds in CI
- Unit tests mock HTTP (no live network)
- Root docs link to the new package
