# TODO

## 2.0

### IChannelServer

- Check the usage of system channels.
- Check `NetworkProtocolLocalChannelClient`.
- HTTP should report channels at `/Connect`.
  - `/Connect` returns `urlRequest;urlResponse` in `HttpClient`.
  - First `/Request` sends channel names.
  - First `/Response` returns `clientId;;` in `NetworkProtocolChannelServer::OnReadString`.
  - How could we response to named pipe?
- Implement async client connection.
- Windows socket
- Linux socket
- stdio?

## Optional
