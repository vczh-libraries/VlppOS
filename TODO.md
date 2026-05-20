# TODO

## 2.0

### IChannelServer

- Check the usage of system channels.
- Check `NetworkProtocolLocalChannelClient`.
- Windows socket
- Linux socket
- stdio?
- Refactoring
  - Initialize underlying resource and check if the callback is installed in `WaitForServer`.
  - Make sure `Stop` blocks until all clients are stopped and resources are released therefore no more messages can be received after `Stop` exits.

## Optional
