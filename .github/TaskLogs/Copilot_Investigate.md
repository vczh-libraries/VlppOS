# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

Restore observability at the raw HTTP protocol layer and reliability policy at the channel layer. A recoverable logical `/Connect`, `/Request`, or `/Response` HTTP failure, non-success response, or invalid response must call `INetworkProtocolCallback::OnLocalError` with `fatal == false`; a direct raw client keeps its generic bounded retry or physical-lane replacement behavior, with exhaustion still raw-fatal. `NetworkProtocolChannelClient` must promote any local error received after the channel has connected to fatal, notify its `IChannelClient` caller with `fatal == true`, and cause the raw transport to stop after the callback returns instead of leaving it in a retry loop. Cover both `windows_http::HttpClient` and `async_tcp_socket::SocketHttpClient`. Do not use error-message text parsing.

Also close the channel-server fatal-broadcast admission race. Once `BroadcastError` takes its target snapshot, no new client may begin admission; an application-accepted client that was already in flight must receive the retained terminal package before rejection instead of committing after the snapshot and missing it.

# TEST [CONFIRMED]

Add focused inter-process tests for the Windows HTTP and async-socket HTTP protocol clients. Force one logical `/Request` exchange to receive 404 or an invalid response and require exactly one raw `INetworkProtocolCallback::OnLocalError` call with `fatal == false`, followed by the existing retry or replacement-poll behavior. Verify that async-socket `/Connect` and `/Response` 404 failures enter their normal retry policy instead of receiving a special immediate-fatal classification. Wrap a raw client in `NetworkProtocolChannelClient`, establish the channel, then inject the same raw nonfatal failure and require one `IChannelClient::OnLocalError` call with `fatal == true`, a disconnected channel, and a stopped raw client without another poll.

The Debug x64 baseline built with zero warnings and errors. The focused `TestInterProcess.cpp` run failed in `SocketHttp reports each Request 404 or invalid response once as nonfatal and retries` because the 404 path reported `fatal == true`. Source inspection also confirmed that `windows_http::HttpClient::OnHttpRequestFailed` retries `/Request` without invoking `RaiseLocalError`, while the async-socket receive path silently replaces transport failures and silently retries invalid successful responses.

The deterministic admission test paused an accepted client's fatal send after `BroadcastError` had taken its snapshot, then released a second client's in-flight application admission. On the baseline, that second client committed and `GetClientIds().Count()` became 2 even though it never received the fatal package.

# PROPOSALS

- No.1 Restore raw HTTP local-error reporting and channel-owned promotion
- No.2 Cover in-flight fatal-broadcast admission before rejection

## No.1 Restore raw HTTP local-error reporting and channel-owned promotion

Report each recoverable logical `/Connect`, `/Request`, or `/Response` HTTP failure, non-success response, or invalid response through the installed `INetworkProtocolCallback` exactly once with `fatal == false`; bounded retry exhaustion remains fatal. Remove the async Socket HTTP 404 fast-fatal branches so the logical endpoint policy is independent from the terminal state of a failed physical lane. Change `INetworkProtocolCallback::OnLocalError` to return whether its owner promotes a nonfatal error to fatal; raw transports OR that decision with their own fatal classification and stop only after the callback returns. `NetworkProtocolChannelClient` returns promotion for every local error received after its channel status becomes `Connected`, forwards `fatal == true` to the `IChannelClient` callback, and disconnects its channel state. Pre-connect retry errors remain nonfatal. This does not require structured error identification or text parsing.

### CODE CHANGE

- Changed `INetworkProtocolCallback::OnLocalError` to return a promotion request. Raw protocols may upgrade their own fatal decision but cannot be demoted.
- Made `NetworkProtocolChannelClient` promote every local error after its channel has ever reached `Connected`, including concurrent errors after the first one transitions channel status to disconnected, forward `fatal == true` to the `IChannelClient` user, and return the promotion to the raw transport without parsing error text.
- Made the Windows HTTP client report `/Request` failures before retrying and honor promotion by synchronously entering logical `Stopping`, which suppresses resubmission. Physical WinHTTP draining remains with an explicit or owning `Stop`; the fatal notification does not depend on `OnDisconnected`.
- Made the async Socket HTTP client report failed or invalid `/Request` exchanges before receive-lane replacement, honor promotion before any replacement or resend, and remove the special immediate-fatal handling of physical 404 results from `/Connect`, `/Request`, and `/Response`.
- Propagated the promotion contract through the direct async-socket network protocol and adjusted the channel-server callback implementation.
- Added focused raw Windows and Socket HTTP tests for 404 and invalid `/Request` responses, Socket HTTP tests for normal `/Connect` exhaustion and `/Response` retry, a Windows promotion/no-retry test, and an end-to-end connected-channel promotion/no-replacement test.
- Updated the network-protocol knowledge base, manual and coding learning notes so physical-lane 404 handling is distinct from logical raw retry policy and channel-owned fatality.

## No.2 Cover in-flight fatal-broadcast admission before rejection

Set a lock-covered terminal-admission flag and retained error in the same critical section in which `BroadcastError` snapshots accepted network and local clients. Reject new raw and local admissions immediately once the flag is set. At both commit points, an application `OnClientConnected` callback that was already in flight cannot publish after the snapshot; if the application accepted it, deliver the retained fatal package (or local read error) before disconnecting it. This covers application-side admission effects such as renderer replacement without allowing the new client to miss the terminal error.

### CODE CHANGE

- Added the `broadcastingError` admission flag and retained first error to `NetworkProtocolChannelServer`.
- Guarded initial raw/local entry and final raw/local commit with the same flag; accepted in-flight clients receive the retained terminal error without receiving an ID.
- Counted raw protocol callbacks, application admission callbacks and fatal-delivery work with a lock-covered stop barrier so neither `BroadcastError` nor a concurrent explicit `Stop` can stop the underlying server before an accepted in-flight client sends or receives its retained terminal error. Concurrent Stop callers share one completion; callback-reentrant broadcast/stop publishes the terminal boundary immediately and defers physical stop until the protected callback unwinds. When the last barrier belongs to a raw callback, a dedicated completion thread calls the underlying server's draining `Stop` off the callback stack, avoiding WinHTTP and named-pipe self-waits; non-reentrant `Stop` callers join and release that helper before returning.
- Kept newly committed network and local clients out of the broadcast snapshot until the client-id response or local `OnConnected` callback completes. A concurrent broadcast then either snapshots the ready client or is retained and delivered afterward, preserving `client id / OnConnected -> fatal -> disconnected` ordering.
- Made terminal broadcast idempotent: the first error wins and later calls do not rebroadcast.
- Made recipient delivery and stop completion exception-safe: delivery continues best-effort, physical shutdown always completes, public callers observe a recorded completion failure at most once, and the documented most-derived destructor suppresses shutdown exceptions.
- Added deterministic network and local-client tests whose fake transport rejects sends after server stop. The tests cover pre-commit and post-commit admissions, concurrent and callback-reentrant Stop/Broadcast calls, client-id and `OnConnected` ordering, an `OnConnected` callback that broadcasts and then throws, retained delivery before disconnection, late admission rejection, raw callback draining, one-shot stop exception reporting, and recipient delivery exceptions.

The final Debug x64 wrapper build completed with zero warnings and errors. The full wrapper-driven UnitTest run passed all 16 test files and all 273 test cases. Debug and Release builds also passed with zero warnings and errors on x64 and Win32. The Linux build could not be run because this Windows machine has no WSL distribution installed. CodePack regenerated the affected common and Windows Release files, and `git diff --check` reported no whitespace errors.
