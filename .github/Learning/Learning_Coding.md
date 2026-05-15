# !!!LEARNING!!!

# Orders

- `NetworkProtocolChannel` queues should stay grouped for `BatchWrite` [1]
- `NetworkProtocolChannel` uses explicit sender and receiver ids [1]
- `vl::inter_process` shutdown must finish callbacks before returning [1]

# Refinements

## `NetworkProtocolChannel` queues should stay grouped for `BatchWrite`

For channel delivery, keep queued packages grouped by target id so each group can be passed directly into `BatchWrite`. Avoid collecting a flat list and regrouping it later when the `Group` structure naturally maintains the batches the transport writer needs.

## `NetworkProtocolChannel` uses explicit sender and receiver ids

Channel messages carry both sender and receiver semantics. Server-originated messages should use `AdminClientId`, client-to-client messages should validate both the sender and receiver client ids, and public names such as `receiverClientId` should reflect the actual direction of delivery.

## `vl::inter_process` shutdown must finish callbacks before returning

Named-pipe and HTTP protocol stop paths must cancel pending work, unregister waits, close active handles, and wait for final callbacks before returning. Unit tests should call `Stop()` as the boundary instead of adding sleeps to hope that callbacks finish later.
