# TODO

## 2.0

- Pass Linux test.
- Pass macOS test.
- Recognize time consume test cases and put them in Release only
  - Reorganize test cases in proper test categories.
  - Some is resolvable when `WINHTTP_OPTION_IPV6_FAST_FALLBACK` is supported.

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Pay attention when `BroadcastFromClient` is happening when the server is accepting new clients.
- stdio?

## Optional
