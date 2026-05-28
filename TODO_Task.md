investigate repro
## Task 1

This task happens in `VlppOS` repo.

Add a helper function `Nullable<WString> HttpServerApi::GetUtf8Body(PHTTP_REQUEST pRequest)`. Make it static as possible.
- Use implementation from `HttpServer` for reading the complete utf8 body, raise exception as exactly what has been done in `HttpServer`.
Add a helper function `void HttpServerApi::SendResponseUtf8(HANDLE httpRequestQueue, HTTP_REQUEST_ID requestId, WString body)`.
- Use `{ 200, WString::Unmanaged(L"OK"), body, L"application/json; charset=utf8" }` to call the original `SendResponse`.
In `HttpServer` and other places should use the above helper functions

## Task 2

This task happens in `GacUI` repo.
Release `VlppOS` and `VlppParser2 to `GacUI`.
`GacUI` already uses the above functions, ensure it compile.
