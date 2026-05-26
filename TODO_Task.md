investigate repro
## Task 1

This task happens in `VlppOS` repo.

General Refactoring:
- `INetworkProtocolClient::WaitForServer` should only be called when status is `Ready`.
  - `NetworkProtocolLocalChannelClient` will skip this check.
- `NetworkProtocolLocalChannelClient`
  - `WaitForServer` should just do nothing, everything should be done in `IChannelServer::ConnectLocalClient`.
  - Common base class with `NetworkProtocolChannelClient` should be extracted as the current base class `NetworkProtocolChannelClient` has members that do not work with `NetworkProtocolLocalChannelClient`.
- `NamedPipeConnection`
  - Remove `lockReadWait` as `readWaitContext` is already atomic.
- `NamedPipeServer::PendingConnection`
  - Remove `lockConnectWait` as `connectWaitContext` is already atomic.
- `HttpServerConnection::InstallCallback`
  - Only move `queuedStrings` in `SPIN_LOCK`.

## Task 2

This task happens in `VlppOS` repo.

- Windows only simple HTTP client APIs, and refactor `HttpClient` `INetworkProtocolClient` implementation.
  - Delete `HttpUtility.h` and `HttpUtility.Windows.cpp`.
  - The API design should be similar to above files but everything would be async.
  - Keep `UrlEncodeQuery`.
  - `HttpClientApi.Windows.(h|cpp)`.

Details:

I would like you to follow the design of `HttpUtility.h` but completely discard the implementation to complete the new API.
By saying `HttpQuery` should be async, I mean the last argumne would be a callback in `Func<void(Variant<HttpResponse, HttpError>)>`, returning `void`.
You are going to decide what could be in `HttpError`. Remember that getting a 404 doesn't count as an error, it counts as an success. Error means anything that is going wrong causing the underlying http client API not working.
If anything is needed from `HttpClient.Windows.cpp` but it does not appear in the original `HttpUtility.h` design, just add anything that is missing.
The underlying http client API needs initialization and finalization, you are just going to add a `HttpClientApi` struct with:
  - All necessary Windows API objects in it.
  - The construction does the initialization and the destructor does the finalization.
  - Delete copy/move ctor, delete copy/move operator=.
  - The class will be created for the same host:port only, sending http requests to different targets requires different instances of `HttpClientApi`.
  - `HttpQuery` becomes its method. Since `HttpClient` only uses utf8, it is OK to make this API only working with `WString`.
  - `HttpEncodeQuery` and `HttpDecodeQuery` becomes its static methods.
Everything should be in `vl::inter_process` namespace.

## Task 3

This task happens in `VlppOS` repo.

- Windows only simple HTTP server APIs, and refactor `HttpServer` `INetworkProtocolServer` implementation.
  - One `HttpServerApi` struct works for one `http://host:port/prefix/.../` prefix.
  - The construction does the initialization and the destructor does the finalization.
  - Delete copy/move ctor, delete copy/move operator=.
  - There will be one callback (abstract function) exposing http server API raw objects to handle the request. One helper function to send status code, optional utf8 reason and utf8 body back, `WString` instead of `U8String`.
  - Optional `HttpVerbOPTIONS` switch. It works for all url under this prefix, it will not passed to the callback.
  - `HttpServerApi.Windows.(h|cpp)`.
Everything should be in `vl::inter_process` namespace.

## Task 4

This task happens in `GacUI` repo.
Release `VlppOS`, import into `GacUI`.
Make sure `RemotingTest_Core` and `RemotingTest_Win32_Renderer` works with `/Http /FCT`.
To verify that, you should launch both processes with the debugger, so that you are able to know the renderer actually communicate commands correctly with core.
You may need to write a piece of temporary powershell script to close the process in a gentle way:
  - You can use whatever way you like, including calling Windows API, to close the renderer gently.
  - Currently `RemotingTest_Win32_Core` and `RemotingTest_Win32_Renderer` crashes during exiting, figure out why and fix that.
    - I don't remember which one crashes, or maybe both, figure out by yourself.
  - After the renderer exits, ensure that core will be notified and exits. If the code has no problem core should already been working that way.
  - Delete that script before commiting.
- Ensure `RemotingTest_*` call `IChannelServer::Stop`, and this function should already been returning after all pending callbacks end, so that you might be able to remove unnecessary blocking code in main/WinMain/GuiMain.

## Task 5

This task happens in `GacUI` repo.

`GuiRemoteProtocolAsyncJsonChannelSerializer` in `GuiRemoteProtocol_Channel_Async.h`
- Rename to `xxxJsonChannelRenderer` and `xxx_Async.(h|cpp)`.

`RemotingTest_Core\GuiMain.cpp`:
- `StartServer` implements `acceptMultipleRenderers` in a wrong way.
  - Now the new named pipe architecture should accept multiple renderers.
  - An renderer can always be accepted and disconnect the previous one.
  - The current implementation does not disconnect the previous one, causing it still able to sends IO events but not rendering.

`GuiRemoteProtocol_Channel_Json.h`
- `GuiRemoteProtocolCoreChannel::Write` when `rendererClientId` is -1, it should stores, and when `Submit` is called without knowing the information, it is discarded.
- `lockRendererClientId` is unnecessary, `atomic_vint` is enough.

`GuiRemoteProtocolAsyncJsonChannelRenderer`
- `SetInvokeInMainThread` should restore missed commands, or just initialize it before `WaitForServer` as an empty window would show before connection, choose one that is easier to do with clean code.
- This class could just be a `IJsonChannelReader` as almost all other members are just redirection:
  - Figure out if it actually reasonable to do that first. I don't want you to force twisting the code.

## Task 6

This task happens in `GacUI` and `GacJS` repo.
Follows `Tools\DebugGacUIWithBrowser.md` to run `RemotingTest_Core` with `/Http /RPT` and make sure `GacJS` could launch and operate the UI.
