# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

TODO_Task_macOS.md , commit all local changes and push to master, rebase if conflict. If new compiling or linking options are needed, change ../Tools/Ubuntu/vl/makefile-cpp, and copy it to VlppOS. Actual test cases in TestInterProcess_AsyncSocket.cpp is not supposed to change unless you have run out of every possible ways, as these test cases are going to serve implementations in 3 different platforms to keep them consistency with each other.

# UPDATES

# TEST [CONFIRMED]

The existing shared cases in `Test/Source/TestInterProcess_AsyncSocket.cpp` are the required behavioral test suite and must remain unchanged apart from filling the prepared `VCZH_GCC && VCZH_APPLE` include, bounded-wait binding, and concrete type invocation placeholders. They cover:

1. Repeated long full-duplex binary transfers, including empty writes, exact submitted-buffer identity, arbitrary positive read blocks no larger than 65536 bytes, and callback shutdown.
2. Server rejection and terminal disconnection.
3. A nested `Stop` from `OnRead`, including the hard callback-drain boundary.
4. A client retry followed by a successful connection.
5. Stopping a client while retry work is pending.

First perform the repository-prescribed full build from `Test/Linux` and run the focused test file to establish the pre-change macOS baseline. The missing implementation is confirmed when the project builds only because the Apple branch is empty and therefore registers none of the shared cases; source inspection must also show that `AsyncSocket.macOS.h/.cpp` and the Apple project/vmake binding are absent.

The proposal succeeds when the full macOS build completes, all five existing shared async-socket cases pass with the Apple binding enabled, the focused process exits successfully without hanging, and review confirms the required callback/ownership/draining contract. No actual test scenario may be edited unless every implementation-only alternative has been exhausted.

## Confirmation

The prescribed clean macOS build succeeds in the current state, but the focused `TestInterProcess_AsyncSocket.cpp` run reports `Passed test cases: 0/0`. Source inspection confirms why: both Apple preprocessor placeholders are empty, `AsyncSocket.macOS.h/.cpp` do not exist, and neither project metadata nor `Test/Linux/vmake` selects a macOS implementation. This reproduces the missing-stage problem without changing the authoritative shared tests.

# PROPOSALS

- No.1 Implement the macOS transport with retained native generations and explicit drain boundaries

## No.1 Implement the macOS transport with retained native generations and explicit drain boundaries

Add PIMPL-shaped `macos_socket::AsyncSocketServer` and `macos_socket::AsyncSocketClient` public classes, with private connection wrappers and state in `AsyncSocket.macOS.cpp`. Give every listener, logical connection, and client attempt its own retained Network.framework object and serial dispatch queue. Copied Network.framework and dispatch Blocks retain explicit state contexts; terminal `cancelled` handlers clear/release native handlers and enqueue a final sentinel on the same queue. External shutdown waits for these sentinels, retry-timer cancellation, locally queued empty-write completions, and active user callbacks. A `Stop` invoked from a connection callback or its serial queue disables ordinary work and synchronously delivers the single terminal disconnection notification, but defers native queue draining so it cannot deadlock itself; a later external `Stop` finishes that drain.

Keep user callbacks outside locks and track callback frames separately from queue identity, including synchronous `OnInstalled`. Accepted connections are fully configured before the accept hook, remember pre-ready read/write requests, and start only when accepted. The client uses fresh numeric-loopback connection generations: `waiting` reports one nonfatal error, cancels and drains that generation, then uses a cancellable one-shot dispatch timer before creating the next generation. `failed` or retry exhaustion enters the single fatal error/disconnection path. Stale generation handlers cannot mutate a replacement.

Receive one bounded block with `nw_connection_receive(..., 1, 65536, ...)`, deliver positive `dispatch_data_apply` regions while borrowed, then process error/EOF and rearm only after all callbacks return. Reserve one write slot, retain the exact submitted `AsyncSocketBuffer`, copy nonempty bytes into explicit `dispatch_data_t` storage, and send with the non-final default message context. Queue one local completion for an empty buffer. Cancellation errors never become user errors.

Add the GCC timed-event prerequisite using one normalized `pthread_cond_timedwait` deadline while preserving existing event reset/counter behavior. Add the macOS files to project metadata, select platform sources and Darwin flags in `Test/Linux/vmake`, and fill only the prepared Apple include/wait/type-binding placeholders in the shared test file. Existing common make support already propagates both option variables and preserves CoreFoundation, so `../Tools/Ubuntu/vl/makefile-cpp` does not require a change.

### CODE CHANGE

Create `Source/InterProcess/AsyncSocket/AsyncSocket.macOS.h` and `AsyncSocket.macOS.cpp`; extend only GCC `ConditionVariable` and `EventObject` timed waiting; update the UnitTest project/filter metadata and Darwin/Linux source selection; and activate the existing Apple shared-test binding with a maximum receive size of 65536. Do not alter any shared test scenario, count, port, timeout, or assertion.

The implementation uses Network.framework connections/listeners with explicit retained generations, serial queues, cancellable retry timers, terminal cancellation sentinels, and copied dispatch-data storage. Callback execution is serialized without holding the state lock during user code. Nested callback shutdown delivers `OnDisconnected` before returning while leaving native state retained for an external drain. Server shutdown has a shared completion barrier, and retry cancellation makes queued `ready`/`failed` updates stale. Reads keep one receive outstanding and buffer any undelivered content while a callback is uninstalled.

The GCC event prerequisite uses a normalized absolute `CLOCK_REALTIME` deadline with `pthread_cond_timedwait`. The project files and `Test/Linux/vmake` select the macOS source with `-fblocks` and `-framework Network`; existing common Darwin make support continues to provide CoreFoundation. The generated `Test/Linux/makefile` and `vmake.txt` were restored after verification, and the shared socket test file changed only in its three prepared Apple placeholders.

### CONFIRMATION

- The prescribed clean macOS build completes and links Network.framework plus CoreFoundation.
- The focused shared socket suite passes all 5/5 cases with exit code 0.
- A pre-hardening stress run passed 1,001/1,001 focused invocations; the final hardened build passed another 500/500 invocations without a crash, hang, malformed summary, or nonzero exit.
- `TestThread.cpp` passes 11/11 cases, and the complete macOS unit suite passes 115/115 cases across 11/11 files.
- Review confirmed explicit native/Block/buffer ownership and identified concurrency races in retry cancellation, overlapping callback-local stops, callback uninstall, fatal ordering, and concurrent server shutdown; all were corrected before the final build and stress run.
- `../Tools/Ubuntu/vl/makefile-cpp` and `.github/Ubuntu/vl/makefile-cpp` remain byte-identical, so no common compiling/linking-option change was needed.
