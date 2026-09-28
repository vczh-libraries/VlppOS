# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

In generated `makefile` we don't really need to do the `rm -f`, we could assume `CPP_TARGET` is always a file in `Bin` folder. Remove this command from `vmake-cpp` in `Tools`, release ubuntu tool to these 4 repos, regenerate updated `makefile` files.
Some little fix, we should actually allow `VlppOS/Source/InterProcess/ChannelImpls/*.*` on all platforms, remove added guards from the last request.
commit and push all local changes.

# TEST

- Regenerate all eight Linux makefiles across Vlpp, VlppOS, VlppRegex and VlppReflection with the canonical toolchain; confirm their clean rules remove Bin without the redundant file-by-file deletion.
- Confirm all seven ChannelImpls files lose only the platform guard added by the previous request, preserving header inclusion guards.
- Build and run native VlppOS tests and the retained Wasm suite. Verify the generated release exposes channel implementations in Wasm and copy the release to the existing downstream consumers.
- Build affected downstream unit tests and check the propagated toolchains match Tools.

# PROPOSALS

- No.1 Simplify generated cleanup and restore portable channel implementations [CONFIRMED]

## No.1 Simplify generated cleanup and restore portable channel implementations

Remove the redundant rm command from the vmake-cpp clean recipe because Bin removal already deletes CPP_TARGET and its Wasm package. Remove the native-platform wrappers from ChannelImpls, whose protocol-independent implementation can use the existing Wasm threading backend. Regenerate VlppOS releases with CodePack and refresh the VlppRegex/VlppReflection imports.

### CODE CHANGE

- Remove the single file-cleanup command from canonical Tools/Ubuntu/vl/vmake-cpp and propagate the Ubuntu toolchain to all four repositories.
- Remove the seven native-platform guards in ChannelImpls, regenerate the release, and refresh the downstream imports.

### CONFIRMED

- All eight native Linux projects built successfully and regenerated their makefiles. The clean recipes remove Bin without the redundant rm -f command, and all four propagated vmake-cpp files match canonical Tools.
- The seven ChannelImpls files are byte-for-byte identical to their contents before the previous WebAssembly request. Header inclusion guards remain intact.
- Native unit tests passed: VlppOS 14 files / 277 cases, VlppRegex 9 files / 226 cases, and VlppReflection 9 files / 53 cases.
- A full VlppOS Wasm rebuild exercised the simplified clean rule. Firefox passed 7 files / 81 cases, with one successful completion and no browser errors.
- A separate Wasm consumer compiled the regenerated release and instantiated the channel, channel-client base, network client, local client and server class layouts. Its browser run passed a channel-package serialization/parsing round trip with client and receiver IDs, alongside the existing unsupported-filesystem and memory-stream checks.
- Regenerated the VlppOS release with CodePack and copied it to VlppRegex and VlppReflection. No generated files were edited by hand.
- Emscripten retains its existing pthread/growable-memory advisory; no build or test failures occurred.
