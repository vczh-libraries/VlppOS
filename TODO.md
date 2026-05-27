# TODO

## 2.0

### IChannelServer

- New tests.
  - Concurrent `SendString` handling.
  - `WaitForServer` without server started.
  - Client or server stops any time.
- Windows socket
- Linux socket
- stdio?
- Make sure `Stop` blocks until all clients are stopped and resources are released therefore no more messages can be received after `Stop` exits.

## Optional
