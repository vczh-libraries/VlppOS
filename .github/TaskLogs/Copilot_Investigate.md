# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

commit and push all local changes

## Task 1
This task is for VlppOS repo
Http(Server|Client)(Api)?.Windows.(h|cpp)  using `vl::inter_process::windows_http` namespace.
NamedPipe.Windows.(h|cpp) using `vl::inter_process::named_pipe` namespace.
update knowledge base in VlppOS to reflect this change

## Task 2
Release VlppOS to Workflow and fix build breaks due to namespace changing
To verify, you need to follow the SOP to run chatbot test projects.

## Task 3
Release VlppOS to GacUI and fix build breaks due to namespace changing.
To verify, you need to:
- follow the SOP to run RemotingTest_(Core|Rendering_Win32) and make sure `/RCP /HTTP` works
- Run GacJS against RemotingTest_Core and make sure `/RCP /HTTP` works

## Task 4
Update document website to reflect the namespace changing, if anything actually updates, publish it

## Task 5
Run [job.Windows.copilotInitAll.prompt.md](Tools/Jobs/job.Windows.copilotInitAll.prompt.md) but skip learning.

# UPDATES

# TEST [CONFIRMED]

Update the existing Windows inter-process test source to import `vl::inter_process::named_pipe` and `vl::inter_process::windows_http` while leaving the generic protocol and channel APIs imported from `vl::inter_process`. A Debug x64 build before the product change must fail because the two requested nested namespaces do not exist yet. This confirms that the current public transport types still live directly in `vl::inter_process`.

After the product change:

1. Build and run the focused `TestInterProcess.cpp` tests in Debug x64. The named-pipe and HTTP raw-protocol and channel scenarios must all pass through the new namespaces.
2. Run the complete Debug x64 unit-test suite and confirm the final summary reports every test passing with no CRT memory-leak report.
3. Build the unit-test solution in Debug/Release and Win32/x64 so every generated Windows release surface compiles with the new namespaces.
4. Regenerate `Release` through the repository code-pack tool, confirm the generated `VlppOS.Windows.h` and `VlppOS.Windows.cpp` contain the new namespace declarations, and confirm no affected concrete transport/helper declaration remains directly in `vl::inter_process`.
5. Review the VlppOS knowledge-base index and detailed inter-process page to ensure every moved public type is documented with its fully qualified namespace while the generic `INetworkProtocol*` and channel APIs remain in `vl::inter_process`.

## Confirmation

The initial Debug x64 unit-test build failed with zero warnings and four compiler errors at the two new using-directives in `TestInterProcess.cpp`. MSVC reported that `named_pipe` and `windows_http` are not members of `vl::inter_process` and that neither namespace exists. The headers cited by the compiler still declare `namespace vl::inter_process`, confirming the exact public-namespace mismatch requested by the task while leaving the existing named-pipe and HTTP behavioral scenarios ready for proposal verification.

# PROPOSALS
