# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

[TODO_Task_MiniHttpApi_NetworkProtocol.md](TODO_Task_MiniHttpApi_NetworkProtocol.md) is almost done but it seems to have a dead lock in the current unit test, you are going to find out and fix it, commit and push all local changes.

# UPDATES

## UPDATE

is the test case running too long so I thought it was deadlock? which test case and how much time was consumed?

# TEST [CONFIRMED]

Use the existing `TestInterProcess.cpp` Socket HTTP matrix and deterministic shutdown cases as the reproduction. Build the Windows Debug x64 UnitTest solution through `copilotBuild.ps1`, then run the focused test file through `copilotExecute.ps1`. If execution stops making progress, use the last completed test output and CDB thread stacks to identify the exact wait cycle instead of adding timing sleeps or weakening the hard `Stop` boundary.

Three current executions completed instead of reproducing a synchronization cycle. The unfiltered Debug x64 suite passed 193/193 cases in about 126 seconds. Two focused `TestInterProcess.cpp` runs passed 19/19 cases in 106.5 and 107.0 seconds with no post-summary CRT leak report. A timestamped focused run isolated the silent time:

- `SocketHttpServer with Windows HttpClient (NetworkProtocol)` consumed 40.86 seconds.
- `SocketHttpServer with Windows HttpClient (Channel)` consumed 40.81 seconds.
- `SocketHttp accepts the UTF-8 body limit and rejects one encoded byte beyond each FIFO` consumed 19.66 seconds while transferring a full 16 MiB logical body in each direction.
- All remaining focused cases together consumed about 5.6 seconds.

The two Windows interoperability cases each repeat 20 times. During their silent interval, the portable server listened only on `127.0.0.1`, while the legacy WinHTTP clients had connections pending from `::1` to the same port. The Windows native async-socket listener uses `AF_INET` and `INADDR_LOOPBACK`, but `windows_http::HttpClient` asks `HttpClientApi` to connect to `localhost`, which resolves to `::1` before `127.0.0.1` on this machine. Default WinHTTP address fallback accounts for the repeated delay. The shutdown cases completed and a textual lock/callback audit found no remaining client or server wait cycle. The apparent deadlock is therefore confirmed as excessive, mostly silent test latency rather than a blocked protocol state.

The fix succeeds when the exact `localhost` HTTP authority and all 20 interoperability repetitions are preserved, both delayed cases complete substantially faster without a timing assertion, the focused file and complete Debug x64 suite pass without a post-summary CRT leak report, and all four Windows solution configurations build with zero errors and no new warnings.

# PROPOSALS

- No.1 Enable WinHTTP IPv6 fast fallback

## No.1 Enable WinHTTP IPv6 fast fallback

Enable `WINHTTP_OPTION_IPV6_FAST_FALLBACK` on the `HttpClientApi` WinHTTP session immediately after `WinHttpOpen`. This is the Windows API's Happy Eyeballs behavior: when a host resolves to both families, the first IPv6 attempt gets a short timeout before WinHTTP tries IPv4. It addresses the owning connection policy without changing the portable IPv4 listener, weakening the 20-iteration interoperability matrix, replacing `localhost`, or changing any public API or successful HTTP wire field.

The option is available starting with Windows 10 version 1903. Treat it as a best-effort performance feature so older supported Windows versions retain WinHTTP's existing fallback behavior instead of failing `HttpClientApi` construction solely because they do not recognize the option. Functional request failures continue to use the existing strict error paths.

### CODE CHANGE

Set the Boolean `WINHTTP_OPTION_IPV6_FAST_FALLBACK` session option in `HttpClientApi::HttpClientApi` after successful session creation and before `WinHttpConnect`. No interface, request, route, retry, listener, or test-repeat change is required.
