# !!!INVESTIGATE!!!

# PROBLEM DESCRIPTION

I think -fblocks, -framework Network and -luring should be monorepo wise, not just for this perticular project. Do you think you can patch makefile-cpp to do that? My eventual goal is that, after running vmake on Linux or macOS, running vmake on another system should not change makefile/vmake.txt and everything is still working. If you find makefile-cpp is the good place, please update the sibiling Tools repo first, and then run `vgo uci VlppOS` to propogate the change to the current repo. `vgo` is in ../Tools/Tools/Ubuntu/vl/cmd. Once you have finished, commit and push both repo, rebase of conflict

# UPDATES

## UPDATE

you can also include AGENTS.md in your commit this is intentional

# TEST [CONFIRMED]

Use the current host-dependent VlppOS UnitTest generation as the reproduction. With both guarded async-socket implementation files retained in the source list, `vmake.txt` must list both files, and the tracked `makefile` must not embed host-selected compiler/linker flags or host-selected header dependencies.

The problem is confirmed on macOS after removing the project-local `uname` branch. Both `AsyncSocket.Linux.cpp` and `AsyncSocket.macOS.cpp` compile, proving that their platform guards work and that the inactive translation unit is harmless. The final link fails with undefined `_nw_*` symbols because deleting the branch also deletes `-framework Network`. The analogous Linux link requires `-luring`. In addition, the current macOS-generated makefile changes the two inter-process test dependency rules from `AsyncSocket.Linux.h` to `AsyncSocket.macOS.h`, because `vmake-cpp` embeds native `clang++ -MM` output. Therefore moving only the flags cannot make the tracked makefile platform-invariant.

Acceptance requires:

- `Tools/Ubuntu/vl/makefile-cpp` to supply `-fblocks` and Network.framework on Darwin and `-luring` on Linux at make execution time.
- The canonical generator to keep tracked makefile rules independent of host preprocessor results while preserving fail-fast dependency validation and correct incremental header rebuilds.
- A normal Darwin generation and a generation with a simulated Linux `uname` result to produce byte-identical `Test/Linux/makefile` and `vmake.txt`.
- Both guarded async-socket source files to remain in `vmake.txt` and the generated link target.
- A clean VlppOS UnitTest build, a second incremental build, generated dependency-file inspection, and the complete UnitTest run to succeed on the available macOS host.
- Canonical Tools changes to be committed and pushed first, followed by `vgo uci VlppOS`, review, verification, and a separate VlppOS commit/push including the intentional `AGENTS.md` change.

# PROPOSALS

- No.1 Centralize platform policy and move header dependencies out of tracked makefiles [CONFIRMED]

## No.1 Centralize platform policy and move header dependencies out of tracked makefiles

Make `Tools/Ubuntu/vl/makefile-cpp` the runtime owner of monorepo-wide platform options. Add a platform compile-option variable, select Darwin and Linux explicitly through make-time `uname`, append the platform compile options to every compiler variant, and keep platform libraries after object prerequisites in every linker variant.

Tracked makefiles cannot become platform-invariant while `vmake-cpp` embeds `clang++ -MM` output, because the active preprocessor branch differs by host. Keep `vutil_CppDependencies` as a fail-fast validation pass, but discard its host-specific output. Emit stable source-only object rules instead. Generate ignored `Obj/*.d` dependency files during the real compilation with `-MMD -MP`, include existing dependency files from `makefile-cpp`, and make each object depend on the shared `makefile-cpp` so the transition rebuild seeds dependency files even for an incremental checkout.

Update the canonical vmake maintenance documentation, verify the Tools copy through VlppOS before publishing it, commit and push Tools, and only then propagate the shared files with `vgo uci VlppOS`.

### CODE CHANGE

In the sibling Tools repository, `Ubuntu/vl/makefile-cpp` now selects shared platform options at make execution time: Darwin compilation uses `-fblocks` and links CoreFoundation plus Network.framework, while Linux links liburing. Every compiler variant writes `Obj/*.d` with `-MMD -MP`, and the shared make fragment includes those ignored dependency files for incremental builds.

`Ubuntu/vl/vmake-cpp` still invokes `vutil_CppDependencies` for fail-fast translation-unit validation, but discards its host-selected dependency output. It maps the selected source list to object names directly and emits source-only tracked rules that also depend on the shared make fragment. The canonical maintenance README documents the new split between generation-time validation and compile-time dependency tracking. These three Tools changes were committed and pushed first as `cf89a96`, then `vgo uci VlppOS` propagated the two shared build files into this repository.

The project-local `Test/Linux/vmake` no longer filters the Linux and macOS async-socket files or owns their platform flags. Regenerated `makefile` and `vmake.txt` contain both guarded implementations and no host-selected headers or platform flags. The intentional `AGENTS.md` clarification is included; no C++ source or test case changed. Before final verification, the branch was rebased onto `c02b312`, and that tip's completed Windows investigation was preserved in the timestamped learning archive before this task log was restored.

### CONFIRMED

A native Darwin generation and a generation with `uname` simulated as Linux produced identical files: `makefile` SHA-1 `5576333d76082892e0246be55387cc134e976d60` and `vmake.txt` SHA-1 `52a139819da9533194cfd1287dc62956963120f2`. Both files list `AsyncSocket.Linux.cpp` and `AsyncSocket.macOS.cpp`, and both objects are linked. The generated object rules contain only their source and `$(VCPROOT)/vl/makefile-cpp`, so native preprocessor results no longer alter tracked output.

On macOS, the repository-local `.github/Ubuntu/build.sh -f` completed with `-fblocks`, `-framework CoreFoundation`, and `-framework Network`; a second incremental build performed no compilation. The clean build created 40 `Obj/*.d` files, and the inter-process test dependency file selects the macOS async-socket header on this host. A Linux-selected make dry run removes `-fblocks` and ends the link with `-luring`. The complete UnitTest suite passes 11/11 test files and 118/118 test cases.
