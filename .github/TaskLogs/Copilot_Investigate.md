# !!!INVESTIGATE!!!
# PROBLEM DESCRIPTION

`ChannelChatData` needs to clean up. `clientId[12]` are just `(tom|jerry)clientId`. So you are going to delete `(tom|jerry)clientId`.

Delete `TextNetworkProtocol.cpp`.

Move `NetworkProtocol(Channel|ChannelClientBase|ChannelClient|LocalChannelClient|ChannelServer)` to new files `(Channel|ChannelClientBase|ChannelClient|LocalChannelClient|ChannelServer)Impl.h`
Move interface declaration from `TextNetworkProtocol.h` to `NetworkProtocol.h`:
- `NetworkPackage` goes to `ChannelPackage.(h|cpp)`, this is not a template class, all function implementations go to the cpp file.
- Now `TextNetworkProtocol.h` only have the long comment, keep the comment and rename the file to `NetworkProtocolChannel.h`, and it includes the new 6 header files.
- New `Channel*Impl.h` files and `ChannelPackage.(h|cpp) should be put in `Common/InterProcess/ChannelImpls` solution folder. - Each header file should includes all necessary headers, you can't mess that up just because `NetworkProtocol.h` is going to include them all.
- All files including `TextNetworkProtocol.h` is going to use `NetworkProtocolChannel.h`. Except that `NetworkProtocol.Windows.h` is going to include `NetworkProtocol.h` instead.

# TEST

- Build `UnitTest` through `.github/Scripts/copilotBuild.ps1`.
- Run the focused unit test execution through `.github/Scripts/copilotExecute.ps1 -Mode UnitTest -Executable UnitTest` with the existing filter.
- Verify there are no stale `TextNetworkProtocol.h` or `TextNetworkProtocol.cpp` references in source or project metadata.
- Verify the channel blocked-receiver assertions still pass after `ChannelChatData` removes the duplicate Tom/Jerry client ids.

# PROPOSALS

- [x] Split `TextNetworkProtocol.h` into `NetworkProtocol.h`, `NetworkProtocolChannel.h`, `ChannelPackage.(h|cpp)`, and the five `Channel*Impl.h` template headers.
- [x] Delete the empty `TextNetworkProtocol.cpp` stub and update MSBuild project/filter entries.
- [x] Update all includes so channel users include `NetworkProtocolChannel.h`, while Windows transport code includes `NetworkProtocol.h`.
- [x] Clean `ChannelChatData` so `clientId1` is Tom and `clientId2` is Jerry directly.

# INVESTIGATION RESULT

[CONFIRMED]

- `NetworkPackage` is now declared in `ChannelPackage.h` and implemented in `ChannelPackage.cpp`.
- `INetworkProtocol*` declarations are now in `NetworkProtocol.h`.
- `NetworkProtocolChannel.h` keeps the long channel protocol comment and includes the channel implementation headers.
- `TextNetworkProtocol.h` and `TextNetworkProtocol.cpp` are removed from source and MSBuild metadata.
- `ChannelChatData` no longer has separate Tom/Jerry id fields; `clientId1` and `clientId2` are the Tom/Jerry ids used by the blocked broadcast assertions.

# VERIFICATION

- `& C:\Code\VczhLibraries\VlppOS\.github\Scripts\copilotBuild.ps1` from `Test\UnitTest`: passed, 0 warnings, 0 errors.
- `& C:\Code\VczhLibraries\VlppOS\.github\Scripts\copilotExecute.ps1 -Mode UnitTest -Executable UnitTest` from `Test\UnitTest`: passed `TestInterProcess.cpp`, 5/5 test cases.
