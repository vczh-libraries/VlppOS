Main goal of this task is to make an implementation of default `IFileSystemImpl` for `VCZH_WASM` based on `OPFS`.
- `FileSystem.Wasm.cpp` with class `OpfsFileSystemImpl` with `GetOSFileSystemImpl` exposed, this function will be called automatically when the file system is first needed.
- When opening a readable file via `GetFileSystemImpl`, you are going to read the whole file and store it into a `MemoryStream` internally, serving `IFileStreamImpl` operations. When `Close` is called and the file is writable, the whole content of `MemoryStream` will be write back to `OPFS`.
- Although the coding convention is limiting the usage of embedded JavaScript in `EM_JS`, but this implementation is not app specific, you can call any JavaScript code to access OPFS in `EM_JS`, keep them short, and do not use the C++ side of OPFS integration.

In order to verify `OpfsFileSystemImpl`, we are going to update web assembly enabled unit test projects in:
- `Vlpp/Test/Linux`: Rewrite `vbuild` to an empty JSON configuration: `{ "WASM=YES": {}}`
- `VlppOS/Test/Linux/UnitTest`: enable following test files with web assembly:
  - `TestFileSystem.cpp`
  - `TestLocale.cpp`
  - `TestStream.cpp`
  - `TestStreamLzw.cpp`
- `VlppRegex/Test/Linux`: enable following test files with web assembly:
  - `TestAutomation.cpp`
- `VlppReflection/Test/Linux/UnitTest`: enable following test files with web assembly:
  - `TestReflection_Builder.cpp`.

The `vbuild` file is currently having a "WASM=YES" stream, and `vbuild -bw|-fbw` will test this sub string to see if a test project could be built with web assembly. Now the rule is changed:
- We are not going to test `WASM=YES`, instead we should test `"WASM=YES"`
- Upon calling `app.sh`, it should give the `vbuild` configuration file to the `app.sh`.
  - Currently all code is copied from `*/Ubuntu/vl/wasm-unittest.*`.
  - I think renaming is a little wired, we should have files sitting in `*/Ubuntu/vl/wasm-unittest/` folder with no renaming.
  - Then we could put `wasm-unittest.html`->`app.html` and `wasm-unittest.sh`->`app.sh` to this file, update `wasm.sh` to fix the file name.
  - We are going to add a `app.js`, currently `app.sh` starts node directly to serve files, we should instead let `app.sh` starts `node` with `app.js`, and `app.js` will start an http service, as well as reading `vbuild` configurations to serve files to `app.html`.
  - Before `app.html` running the unit test, it should ask the http server for the file list:
    - If `OPFS` already stores something for this website, clean everything.
    - GET `/OPFS` -> a list of file names, or a tree, you make your decision according to the convenience of calling OPFS to prefill files, this request returns JSON.
    - GET `/OPFS/path/to/the/file` -> `app.js` will read the file from disk and the request gets the binary content, `app.html` and then stores the file to `OPFS`.
    - Now `OPFS` has prefilled files, we can run unit test and the unit test has access to file system.
    - When a file is changed, we don't need to write it back to the disk.
  - Fix everything in `Tools` and release the ubuntu tool to all mentioned 4 repos.
