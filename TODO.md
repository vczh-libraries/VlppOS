# TODO

## 2.0

- Review how mini heep servers share the same socket server.
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

### TUI

- `TuiPlayground`:
  - Windows version should disable scrollbar when possible.
  - Implement a drawing app with command typing.
- Move key definitions and convertions from GacUI/iGac/wGac to VlppOS.

## Optional
