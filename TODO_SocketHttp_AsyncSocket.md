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
  - Text block encoded in length in WString::Length + non-zero-terminated wchar_t characters. Similar to `NamedPipe.Windows.(h|cpp)`.
  - unit test (shared)

## Async Socket Implementations

All implementations use IPv4 TCP bound or connected directly to `127.0.0.1`; they do not resolve `localhost` and never bind the server to every network interface. Creating native objects and configuring a listener are synchronous setup operations. Accepting a client, connecting, reading and writing are completion-driven operations.

`IAsyncSocketServer::Start` performs the one-time listener setup and starts the platform's asynchronous acceptance mechanism before returning. `IAsyncSocketClient::WaitForServer` may block its caller to preserve the public contract, but the actual connect attempt and all retry/cancellation work must remain asynchronous; it must never block a platform completion thread or dispatch queue. Retry count and delay are one cross-platform client policy outside this platform mapping. A retry is scheduled asynchronously and creates or restarts the native connection as required; no implementation should use a blocking sleep on its completion thread.

Each native operation retains its connection, operation state and buffers until completion. Platform callbacks invoke user callbacks without holding implementation locks. `Stop` first prevents new work, cancels native work, and drains outstanding completions before releasing native state. Calling `Stop` inside one of its own callbacks is the sole exception to waiting for that current callback: it prevents further ordinary callbacks, waits for any other active callbacks, performs the terminal notification before returning, and keeps detached native state alive until the current and cancellation callbacks unwind. Intentional cancellation does not produce `OnError`, and terminal disconnection produces `OnDisconnected` exactly once, including an explicit `Stop`; it is never posted as a user callback after `Stop` returns.

### Windows

Use Winsock 2 overlapped sockets with an I/O completion port (IOCP).

- Initialize a shared Winsock runtime with `WSAStartup` and balance it with `WSACleanup` only after all sockets and workers have stopped. Create sockets with `WSASocketW` and `WSA_FLAG_OVERLAPPED`. Create an IOCP with `CreateIoCompletionPort`, associate the listener and connected sockets with it, and let a small number of workers dequeue completions with `GetQueuedCompletionStatus`. No thread is needed per connection.
- Server startup uses synchronous `socket` creation, `bind` to `INADDR_LOOPBACK`, and `listen`. `SO_EXCLUSIVEADDRUSE` is appropriate because the service owns its port. Load `AcceptEx` with `WSAIoctl` and keep one accept pending. Use zero initial-data length so the accept completes when TCP connects instead of waiting for application bytes. After a successful completion, apply `SO_UPDATE_ACCEPT_CONTEXT`, associate the accepted socket with the IOCP, post the next accept while the server is running, and pass the connection to `OnClientConnected`; close it when rejected.
- Client connection uses `ConnectEx`, also loaded with `WSAIoctl`. `ConnectEx` requires the socket to be bound first, so bind it to an ephemeral local endpoint. After completion, apply `SO_UPDATE_CONNECT_CONTEXT`, update `ClientStatus`, call `OnConnected`, and wake `WaitForServer`. A retry after failure closes this socket and creates a new one.
- `BeginReadingLoopUnsafe` posts one overlapped `WSARecv`. A positive completion becomes one borrowed `OnRead` block, a zero-byte completion is EOF, and the next receive is posted only after `OnRead` returns. `WriteAsync` uses overlapped `WSASend`, retains the `AsyncSocketBuffer`, and continues from the reported offset if the completion is short. Call `OnWriteCompleted` only after the whole buffer is sent.
- `closesocket` cancels outstanding socket operations during `Stop`, but their IOCP completion packets still have to be drained. Expected `WSA_OPERATION_ABORTED` completions are shutdown bookkeeping, not `OnError`. Keep every `WSAOVERLAPPED` context and buffer alive until its final packet has been consumed.

References: [I/O completion ports](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports), [`AcceptEx`](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nf-mswsock-acceptex), [`ConnectEx`](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_connectex), [`WSARecv`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv), [`WSASend`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend).

### Linux

Use `liburing` as the userspace interface to `io_uring`. The Linux target must include and link `liburing`.

- Initialize one ring for a server or client with `io_uring_queue_init_params` and run a completion loop using `io_uring_submit` and `io_uring_wait_cqe`. Store a unique operation context in each SQE's `user_data`, and consume each CQE with `io_uring_cqe_seen`. Serialize submissions if public methods can be called from arbitrary threads, and release the drained ring with `io_uring_queue_exit`.
- Server startup uses ordinary synchronous `socket`, `bind` to `INADDR_LOOPBACK`, and `listen`; these calls are sufficient here and avoid requiring newer asynchronous socket-setup opcodes. Submit one `io_uring_prep_accept`; after each successful or recoverable completion, rearm it while the server is running, wrap the returned file descriptor, and pass it to `OnClientConnected`. Close the descriptor when rejected.
- Client connection creates its socket synchronously and submits `io_uring_prep_connect`. A zero CQE result means connected; a negative result is `-errno`. Update `ClientStatus`, call `OnConnected`, and wake `WaitForServer` from the completion path. Create a fresh socket before retrying a failed connect.
- `BeginReadingLoopUnsafe` submits one `io_uring_prep_recv`. A positive result becomes `OnRead`, zero is EOF, and the next receive is submitted after the callback returns. `WriteAsync` uses `io_uring_prep_send` with `MSG_NOSIGNAL`, retains the buffer, and resubmits any unsent suffix after a short send before calling `OnWriteCompleted`.
- `Stop` submits `io_uring_prep_cancel64` for outstanding accept, connect, receive and send operations, then drains their CQEs before freeing contexts or closing the ring. Closing a file descriptor alone is not a substitute for cancelling an `io_uring` request.
- Use single-shot accept and receive; this interface does not need multishot operations or registered buffers. If the minimum Linux kernel is not fixed by deployment, check the required operations with `io_uring_get_probe_ring` and `io_uring_opcode_supported` during startup.

References: [`liburing`](https://github.com/axboe/liburing), [`io_uring` networking overview](https://github.com/axboe/liburing/wiki/io_uring-and-networking-in-2023), [ring initialization](https://github.com/axboe/liburing/blob/master/man/io_uring_queue_init.3), [accept](https://github.com/axboe/liburing/blob/master/man/io_uring_prep_accept.3), [connect](https://github.com/axboe/liburing/blob/master/man/io_uring_prep_connect.3), [receive](https://github.com/axboe/liburing/blob/master/man/io_uring_prep_recv.3), [send](https://github.com/axboe/liburing/blob/master/man/io_uring_prep_send.3), [cancellation](https://github.com/axboe/liburing/blob/master/man/io_uring_cancelation.7).

### macOS

Use the C API of Network.framework (`nw_listener_t` and `nw_connection_t`) with Grand Central Dispatch. Create plain-TCP parameters with `nw_parameters_create_secure_tcp`, passing `NW_PARAMETERS_DISABLE_PROTOCOL` for TLS. The macOS target must enable Clang Blocks support and link Network.framework.

- Restrict the server parameters to the specific local endpoint `127.0.0.1:{port}` with `nw_parameters_set_local_endpoint`; a listener configured only with a port may accept on other interfaces. The client uses a numeric loopback endpoint created by `nw_endpoint_create_host`.
- Server setup creates an `nw_listener_t`, installs its state and new-connection handlers, assigns a serial dispatch queue, and calls `nw_listener_start`. Object construction is synchronous, but readiness and setup failure arrive asynchronously through the state handler. For each delivered `nw_connection_t`, configure its wrapper, queue and state handler before offering it to `OnClientConnected`; start it when accepted or cancel it when rejected. If the callback requests reading, remember that request until the accepted connection becomes `ready`.
- Client setup creates an `nw_connection_t`, assigns a serial dispatch queue and state handler, and calls `nw_connection_start`. The `ready` state updates `ClientStatus`, calls `OnConnected`, and wakes `WaitForServer`. The `waiting` state is nonfatal; use `nw_connection_restart` or a replacement connection according to the common retry policy. The `failed` state reports a fatal error and cancels the connection, and `cancelled` is terminal.
- `BeginReadingLoopUnsafe` uses `nw_connection_receive`, with a minimum of one byte and a bounded block-sized maximum. Each call has one completion. Deliver the returned `dispatch_data_t` regions as borrowed `OnRead` blocks, then schedule the next receive after all callbacks return. Content can arrive together with EOF or an error, so deliver the content first and then cancel the connection; this interface does not expose a read-half-closed state.
- `WriteAsync` wraps the retained `AsyncSocketBuffer` in `dispatch_data_t` and calls `nw_connection_send` with an ordinary non-final stream context. Network.framework handles partial TCP writes internally. A successful completion becomes `OnWriteCompleted`; a completion error is fatal and cancels the connection.
- `nw_connection_cancel` and `nw_listener_cancel` are asynchronous. Server shutdown separately cancels every connection already delivered by the listener. Keep native objects and callback state alive until each terminal `cancelled` state and the tracked callbacks finish; an empty serial queue alone is not a cancellation boundary. A callback running on that same queue must never synchronously wait for its own cancellation handler.

References: [`nw_listener_t`](https://developer.apple.com/documentation/network/nw_listener_t?language=objc), [`nw_connection_t`](https://developer.apple.com/documentation/network/nw_connection_t?language=objc), [local endpoint selection](https://developer.apple.com/documentation/network/nw_parameters_set_local_endpoint%28_%3A_%3A%29), [receive](https://developer.apple.com/documentation/network/nw_connection_receive%28_%3A_%3A_%3A_%3A%29), [send](https://developer.apple.com/documentation/network/nw_connection_send%28_%3A_%3A_%3A_%3A_%3A%29).
