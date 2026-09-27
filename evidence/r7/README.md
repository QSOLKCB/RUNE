# R7 Evidence Bundles

This directory is intentionally empty of performance conclusions until a local
R7 study is executed.

Create a bundle with:

~~~sh
./scripts/r7-run-local.sh
~~~

or choose an explicit destination:

~~~sh
RUNE_R7_REPEATS=5 CC=cc ./scripts/r7-run-local.sh evidence/r7/my-host
~~~

The destination must not already exist; evidence bundles are immutable capture
records and are never overwritten. Destinations that resolve inside or
lexically route through the repository `build/` tree are rejected, and
parent-directory traversal is not accepted.

A bundle contains:

- `environment.txt` — evidence class, exact source revision, verified clean
  worktree state (including no untracked files), timer method, exact
  CPPFLAGS/CFLAGS, mandatory platform identity, CPU context, and required memory
  profile;
- `compiler.txt` — resolved compiler and archiver paths plus successful
  identity/version output;
- `command.txt` — exact replayable build/run commands using POSIX shell-safe
  quoting, including multiword values and embedded apostrophes;
- `observations.tsv` — raw numeric R7 observations;
- `SHA256SUMS` — integrity hashes produced by a trusted-path SHA-256 utility
  whose absolute path/identity are recorded in the bundle.

Evidence capture requires a clean Git working tree. Tracked, staged, or
untracked changes cause a fail-closed exit before the bundle directory is
created, so source_revision identifies the exact contents built.
`assume-unchanged` and `skip-worktree` index flags are forbidden because
they can hide tracked modifications from ordinary status checks. Git routing
overrides are cleared and all provenance reads are rooted at the repository.
Git itself is resolved from a fixed provenance path
(`/usr/bin:/bin:/usr/sbin:/sbin`) and its absolute path/version are recorded;
the overridable build path cannot substitute the Git executable. Git
replacement objects are disabled, active replacement refs are rejected, and
tracked Makefile/source/include/study files are raw-hashed with filters disabled
and must match their blobs at `source_revision`. Ignored untracked files under
`src/`, `include/`, or `study/` are also forbidden because they can
satisfy compiler includes without appearing in ordinary status output.

For evidence capture, CC and AR must each identify one executable; compound
commands are rejected. Ambient compiler search variables (CPATH,
C_INCLUDE_PATH, CPLUS_INCLUDE_PATH, OBJC_INCLUDE_PATH, COMPILER_PATH,
LIBRARY_PATH, GCC_EXEC_PREFIX) are cleared; intentional include paths belong in
the recorded CPPFLAGS. Dynamic-loader injection/search variables
(`LD_PRELOAD`, loader library paths/auditing, relevant `DYLD_*` variables,
`LIBPATH`, and `SHLIB_PATH`) are cleared before toolchain identity and study
execution. CI exercises hostile compiler-search, Git-routing, ignored-header,
and loader-injection cases. Missing compiler identity, a failed/empty `uname -a` platform identity, or
missing memory profile causes the capture to fail. The required `uname`
platform probe is resolved from the fixed provenance path rather than ambient
`PATH`. The getconf fallback requires both a nonzero numeric physical page
count and page size.

Inherited MAKEFLAGS/GNUMAKEFLAGS/MFLAGS/MAKEFILES/MAKEOVERRIDES are cleared for
the evidence build. Ambient `PATH` is replaced by a recorded sanitized build
path (default `/usr/bin:/bin:/usr/sbin:/sbin`), from which make/mkdir/rm are
resolved. CPPFLAGS/CFLAGS metadata is emitted with `printf`, preserving
backslashes exactly. Each
capture builds into a fresh unique `BUILD_DIR` keyed by UTC timestamp plus
the shell PID and never relies on `make clean`, so concurrent captures do not
share objects/executables and a fake ambient `rm` cannot preserve and certify
a stale study executable.

The evidence build also uses the tracked repository Makefile explicitly via
the resolved absolute make path with `-C <repo> -f Makefile`. The Make target
and `BUILD_DIR` passed into Make are repository-relative so repository paths
containing spaces remain parseable; the executable used by the shell remains
absolute. The bundle records the build-driver identity and Makefile Git blob
identity. Ignored
`GNUmakefile` or lowercase `makefile` files cannot replace the build recipe.
The recorded run command uses an absolute study executable path and includes
the absolute redirection to `observations.tsv`, making `command.txt`
replayable from outside the repository while recreating the captured output
artifact.

Raw observations are execution evidence, not universal performance claims.
Interpretation belongs in a separately reviewed evidence commit.

GitHub-hosted CI runs only `r7-study-smoke` to prove the harness builds,
executes, preserves comparison result identity, and emits the expected schema.
CI timing values are not R7 performance evidence.
