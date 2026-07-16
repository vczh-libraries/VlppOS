# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

You accidentally stopped, I have committed all local changes so far, and I run the unit test myself and find `SocketHttpServerApi maps malformed wire requests to structured 400 408 413 414 417 431 501 and 505 responses` does not finish in 10 seconds so I killed it. Please continue to investigate and fix the issue, finish all the work, commit and push to master

# UPDATES

# TEST [CONFIRMED]

The named test sends four immediately malformed requests, then sends an incomplete HTTP/1.1 header to exercise `408 Request Timeout`. The fifth probe passed a 45-second wait to `ProbeWireStatus`, while the product constant `HttpIncompleteMessageTimeout` is 30 seconds. `HttpRequestConnection::ProcessBufferedInput` therefore armed the production 30-second incomplete-message deadline, and the server could not produce the expected 408 within the user's 10-second observation window. The earlier focused run consequently took about 35 seconds. This confirms that the test was intentionally sleeping on a production deadline rather than exposing a parser, response-write, disconnect, or server-stop deadlock.

The regression criterion is to preserve the production 30-second policy and the complete native exchange: the parser must observe incomplete input, arm the deadline, report structured `RequestTimeout`, serialize a 408 response, complete the write, and close the connection. The test must trigger that deadline through a deterministic fake seam with bounded event coordination and no sleep. The focused MiniHttp file must then finish below 10 seconds, and the complete Debug x64 suite and all four Windows build configurations must remain clean.

A repeated complete-suite run exposed a second timeout race in the later client response-deadline case. Native debugger stacks showed the external `SocketHttpClientApi::Stop` thread inside `HttpRequestTimeoutController::CancelAndWait`, waiting for the firing timeout callback, while that callback had finished `OnError` and entered `HttpRequestConnection::StopConnection` as a non-callback follower, waiting for the external stop to set `stopFinished`. This cycle is separate from the original 30-second 408 delay and requires deterministic raw-layer regression coverage.

# PROPOSALS

- No.1 Inject a deterministic per-connection server timeout controller [CONFIRMED]

## No.1 Inject a deterministic per-connection server timeout controller

Keep `HttpIncompleteMessageTimeout` at 30 seconds. Preserve the existing public one-argument `HttpRequestServer` constructor and add a protected overload that accepts a `Func<Ptr<IHttpRequestTimeoutController>()>`. Store the factory immutably in the server lifecycle, invoke it outside server locks for every accepted native connection, require a fresh non-null controller, and pass it to the existing `HttpRequestConnection` injection point.

Thread a snapshot of this factory through the private MiniHttp shared-server construction path. A test-only setter/resetter lives beside the existing private listener-factory seam and may change the factory only while the MiniHttp registry has no entries. Copy factory functions while holding the registry lock, but construct controllers and listeners only after releasing it.

In the shared MiniHttp test, use a selective factory that returns ordinary production controllers for every connection except the next explicitly selected one. Give only the incomplete-header 408 probe a manual controller. The probe's post-write hook waits for the controller's manual-reset armed event and calls `Fire`; the controller mirrors production self-cancel behavior with a critical section, condition variable, firing flag, and thread-local nested-callback detection. This avoids a self-deadlock when `ReportRequestFailure` calls `CancelAndWait` from inside the fired callback. It also proves the parser actually armed the 30-second duration before the test advances it.

Track execution of the callback passed to every timeout controller with a connection-local thread-local frame. If an external stop already owns shutdown, a stop request made by that firing timeout callback is a follower and must return immediately; the external initiator will resume as soon as the timeout callback unwinds, then perform native stop, disconnect notification, and callback drain. A timeout callback that wins the initial stop race still performs the complete stop itself. This mirrors the existing callback/socket-callback follower policy without exposing controller implementation details.

### CODE CHANGE

- Added the protected timeout-controller-factory overload to `HttpRequestServer`, retained the original public constructor symbol, and created one injected controller per accepted connection outside lifecycle locks.
- Added a private empty-registry-guarded timeout factory to the MiniHttp registry and passed its snapshot to `SharedServer` without changing `SocketHttpServerApi`'s public surface.
- Added a reentrancy-safe manual timeout controller, selective per-connection factory, RAII reset scope, and post-write hook to `TestInterProcess_AsyncSocket_MiniHttpApi.cpp`.
- Replaced the real 45-second 408 probe wait with deterministic arm-and-fire coordination, while asserting one arm and the unchanged `HttpIncompleteMessageTimeout` duration. The test intentionally permits refreshes because TCP may split one write across multiple reads.
- Added a timeout-callback frame to `HttpRequestConnection` so a firing timeout cannot wait behind an external stop that is already waiting for that callback, plus a barrier-controlled raw regression that forces this exact ownership order.

### CONFIRMED

The final focused Debug x64 MiniHttp run completed in 2.53 seconds and passed 1/1 test file and 15/15 cases. The named malformed-wire case still traversed the native socket server, raw HTTP parser, structured 408 callback, response serializer, write completion, and connection-close boundary; only the clock source was controlled. The focused raw HTTP file passed 1/1 file and 38/38 cases, including the new external-stop/firing-timeout ownership regression. Two consecutive complete Debug x64 runs then passed 15/15 files and 181/181 cases, and the final `Execute.log` contains no CRT memory-leak dump.

The repository build wrapper completed Debug and Release for Win32 and x64 with zero warnings and zero errors in every configuration. The final retained Debug x64 `Build.log` also ends with zero warnings and zero errors. Linux and macOS were not executed on Windows; the changed factory types, lifecycle storage, shared-server path, test runner, event coordination, and guarded native instantiations are platform-neutral and were reviewed textually.
