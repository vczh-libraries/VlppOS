# TODO

## 2.0

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Pay attention when `BroadcastFromClient` is happening when the server is accepting new clients.
- Windows socket
- Linux socket
- stdio?
- Make sure `Stop` blocks until all clients are stopped and resources are released therefore no more messages can be received after `Stop` exits.

## Optional
