# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

- Move `GacUI/Test/RemotingHelpers/AutomationService` folder to `VlppOS/Source/InterProcess`, change its namespace to `vl::inter_process::stdio_redirection`. No unit test is needed for VlppOS.
- Update knowledge base in VlppOS repo to mention about stdio redirection. If it is already mentioned in GacUI's knowledge base, remove it. stdio redirection will be part of VlppOS's inter process, still for testing only.
- Release VlppOS to GacUI and make sure GacUI builds. No test is needed, probably a little fix of namespace and "#include". Update GacUI/Release.
- commit and push all local changes after finishing

# UPDATES

# TEST [CONFIRMED]

No unit test is required by the request. Regenerate VlppOS and GacUI release artifacts, then build `GacUI/Test/GacUISrc/GacUISrc.sln` in the default Debug x64 configuration. Success requires a clean build with zero errors and no stale source/project references to the old GacUI helper path.

# PROPOSALS

- No.1 MOVE STDIO REDIRECTION TO VLPPos INTER-PROCESS [CONFIRMED]

## No.1 MOVE STDIO REDIRECTION TO VLPPos INTER-PROCESS

Move the existing `GacUI/Test/RemotingHelpers/StdioRedirection` implementation—the concrete helper matching the requested feature—into `VlppOS/Source/InterProcess/StdioRedirection`. Publish it from the `vl::inter_process::stdio_redirection` namespace through the ordinary neutral and platform VlppOS release pairs. Remove GacUI's duplicate helper inventory, consume the imported VlppOS API, and transfer concrete design/API guidance to the VlppOS knowledge base while retaining the testing-only restriction.

### CODE CHANGE

Moved the four `StdioRedirection` source files from GacUI into `VlppOS/Source/InterProcess/StdioRedirection`, changed their namespace to `vl::inter_process::stdio_redirection`, added direct VlppOS source dependencies, and registered the files in Windows and generated portable project inventories. Added the testing-only API and design guidance to the VlppOS knowledge base and moved the reusable learning note to VlppOS.

Regenerated all VlppOS release and IncludeOnly pairs, copied the six public release files to GacUI `Import`, and verified each copy by SHA-256. GacUI consumers now include the imported VlppOS API and use the new namespace. Removed the old helper source inventory and stale GacUI ownership notes, regenerated Linux source inventories, regenerated `GacUI/Release`, and removed the obsolete platform-specific `Test.RemotingHelpers` pairs now supplied by VlppOS.

### CONFIRMED

`GacUI/Test/GacUISrc/GacUISrc.sln` built successfully in Debug x64 with 0 warnings and 0 errors. No unit tests were run, as requested. Final searches found no non-generated source references to the old GacUI helper path or namespace, and all six GacUI `Import/VlppOS*` files match their upstream VlppOS release files byte-for-byte.
