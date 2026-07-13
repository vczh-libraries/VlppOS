# IAsyncSocket(Server|Client)

Interface in `AsyncSocket/AsyncSocket.h`
Implementations in `(Windows|Linux|macOS)/AsyncSocket.(windows|linux|macos).(h|cpp)`
Using namespace `vl::inter_process::async_tcp_socket(::(windows|linux|macos)_socket)?`

- Binary async-only interface implemented in:
  - Windows
  - Linux
  - macOS
  - unit test
- Focus on async binary data accessing, pattern like `read_some`, which it push data to users, users can't request for a specific length.
- Connect to current machine (127.0.0.1) only with user-specified port

## Interface proposal:

The design is similar to `INetworkProtocol(Server|Client|Connection|Callback)`

```C++
class AsyncSocketBuffer : public Object
{
public:
	collections::Array<vuint8_t>                data;
};

class IAsyncSocketConnection;

class IAsyncSocketCallback : public virtual Interface
{
public:
	// The buffer is borrowed and is valid only during this callback.
	// One callback represents one arbitrary read_some result.
	virtual void                                OnRead(const vuint8_t* buffer, vint size) = 0;

	// Called after the complete buffer passed to WriteAsync has been sent.
	virtual void                                OnWriteCompleted(Ptr<AsyncSocketBuffer> buffer) {}

	virtual void                                OnError(const WString& error, bool fatal) {}
	virtual void                                OnConnected() {}
	virtual void                                OnDisconnected() {}
	virtual void                                OnInstalled(IAsyncSocketConnection* connection) = 0;
};

class IAsyncSocketConnection : public virtual Interface
{
public:
	virtual void                                InstallCallback(IAsyncSocketCallback* callback) = 0;

	// Continuously perform read_some and deliver every result through OnRead.
	virtual void                                BeginReadingLoopUnsafe() = 0;

	// Send the whole buffer in order. The implementation handles partial OS writes.
	virtual void                                WriteAsync(Ptr<AsyncSocketBuffer> buffer) = 0;

	virtual void                                Stop() = 0;
};

class IAsyncSocketClient : public virtual Interface
{
public:
	virtual IAsyncSocketConnection*             GetConnection() = 0;
	virtual void                                WaitForServer() = 0;
	virtual ClientStatus                        GetStatus() = 0;
};

class IAsyncSocketServer : public virtual Interface
{
public:
	virtual WaitForClientResult                 OnClientConnected(IAsyncSocketConnection* connection) = 0;
	virtual void                                Start() = 0;
	virtual void                                Stop() = 0;
	virtual bool                                IsStopped() = 0;
};
```

The connection is an ordered full-duplex byte stream. It deliberately does not expose packets, TCP segments or a requested read length.

- `BeginReadingLoopUnsafe` starts a continuous callback-driven `read_some` loop, matching `INetworkProtocolConnection`. After it is called, the implementation keeps exactly one read outstanding and schedules the next read after the current `OnRead` callback returns. The consumer cannot request a byte count, pause between protocol states or decide what one callback contains.
- `OnRead` may contain any positive number of bytes chosen by the implementation. It can split or combine protocol elements arbitrarily. The callback must consume the bytes or copy any required remainder before returning.
- Only one write may be outstanding. `WriteAsync` retains the supplied `Ptr<AsyncSocketBuffer>` until `OnWriteCompleted`, sends all bytes in order, and hides platform-specific partial writes. A user that has multiple buffers maintains its own write queue and submits the next buffer from its state machine.
- One read and one write may be outstanding simultaneously. Callbacks can run on any thread, so the callback and connection state must be thread-safe.
- `OnDisconnected` represents EOF or loss of the peer. A fatal `OnError` is followed by disconnection. A nonfatal error leaves the connection usable according to the operation-specific contract.
- `InstallCallback` accepts one callback and uses `nullptr` to uninstall it, matching `INetworkProtocolConnection`.
- `Stop` is the shutdown boundary. It cancels pending operations and waits until callbacks that could access the connection owner have finished before returning.

This small contract is sufficient for the buffered state machine described in `Efficient TCP Socket Async Reading`: the connection issues efficient block reads continuously, while the user-owned state machine preserves surplus bytes and parses as much as possible whenever `OnRead` pushes another arbitrary block. Because the next read is scheduled only after the callback returns, callback execution provides basic backpressure without exposing read scheduling to the consumer.

## INetworkProtocol(Server|Client) on IAsyncSocket(Server|Client)

Implementations in `AsyncSocket/AsyncSocket.h`
Using namespace `vl::inter_process::async_tcp_socket`
Offer template classes, take an actual implementation of `IAsyncSocket*`, and implement `INetworkProtocol*`

- INetworkProtocolServer / INetworkProtocolClient:
  - Text service based on socket.
  - Text block encoded in length in bytes + non-zero-terminated utf-8 string.
  - unit test (shared)

## Async Socket Implementations

### Windows

<!-- Explain IOCP -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async -->

### Linux

<!-- Explain io_uring -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async -->

### macOS

<!-- Explain Framework.Network -->
<!-- Starting an Socket Sync, or if it forced us Async -->
<!-- Waiting for a client Async -->
<!-- Reading/Writing data Async -->
