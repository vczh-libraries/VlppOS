# !!!LEARNING!!!

# Orders

- `NetworkProtocolChannel` queues should stay grouped for `BatchWrite` [1]
- `NetworkProtocolChannel` uses explicit positive sender and receiver ids [1]
- Start `INetworkProtocolServer` / `IChannelServer` after construction [1]
- `vl::inter_process` shutdown must finish callbacks before returning [1]
- Keep `IChannelServer` delivery-only; use local clients for server speech [1]
- `NetworkProtocolLocalChannelClient` owns server-local behavior [1]
- Group `SpinLock`-protected fields with coverage comments [1]
- `HttpServerConnection` queues pending outbound `/Request` responses [1]

# Refinements

## `NetworkProtocolChannel` queues should stay grouped for `BatchWrite`

For channel delivery, keep queued packages grouped by target id so each group can be passed directly into `BatchWrite`. Avoid collecting a flat list and regrouping it later when the `Group` structure naturally maintains the batches the transport writer needs.

## `NetworkProtocolChannel` uses explicit positive sender and receiver ids

Channel messages carry both sender and receiver semantics. Ordinary channel chat should use real positive client ids for both sides; do not use `AdminClientId` as the sender for normal server-originated messages. If server-side behavior needs to speak on the channel, connect it as a local client and send from the assigned id. Validate both sender and receiver client ids, and keep public names such as `receiverClientId` aligned with the actual delivery direction.

## Start `INetworkProtocolServer` / `IChannelServer` after construction

For `vl::inter_process`, transport and channel servers should initialize handles and state in constructors, but begin named-pipe or HTTP accept callbacks only from `Start()`. Channel servers should reject remote and local clients before they are started, and callers should start the channel layer and transport layer only after concrete server construction is complete.

## `vl::inter_process` shutdown must finish callbacks before returning

Named-pipe and HTTP protocol stop paths must cancel pending work, unregister waits, close active handles, and wait for final callbacks before returning. Unit tests should call `Stop()` as the boundary instead of adding sleeps to hope that callbacks finish later.

## Keep `IChannelServer` delivery-only; use local clients for server speech

Keep `IChannelServer` focused on accepting clients and delivering protocol messages. Do not expose a `GetChannel` escape hatch or let `NetworkProtocolChannelServer` own user channel objects for ordinary chat. When the server needs to participate as a speaker, create a server-side local channel client, register it through `ConnectLocalClient`, and let it send/receive through the same channel contract as other clients.

## `NetworkProtocolLocalChannelClient` owns server-local behavior

Keep local-client state and local-server branches out of `NetworkProtocolChannelClient`. Put the server-side local connection pointer, local wait/disconnect handling, local send routing, and local error broadcasting in `NetworkProtocolLocalChannelClient`; override the local branch there and call the base method for common client work. `NetworkProtocolChannelServer::ConnectLocalClient` can require this subtype (for example with `dynamic_cast`) so no extra lock is needed just to protect a `localServer` field on ordinary network clients.

## Group `SpinLock`-protected fields with coverage comments

For classes that use `SpinLock`, group every field protected by a given lock directly under that lock and add a short comment naming the coverage. This makes it clear which state must be accessed while holding each lock, especially in inter-process protocol and test helper code with multiple asynchronous paths.

## `HttpServerConnection` queues pending outbound `/Request` responses

When HTTP inter-process channel delivery can produce multiple outbound responses back-to-back, queue pending `/Request` responses instead of storing one overwriteable pending response. This preserves sequences such as the connection response followed immediately by the first channel message.
