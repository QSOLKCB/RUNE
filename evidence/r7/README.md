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
records and are never overwritten. Capture output is first staged under the
ignored repository `build/` tree. The requested destination is published only
after successful measurement, final provenance validation, snapshot cleanup,
and SHA-256 generation. Destinations that resolve inside or
lexically route through the repository `build/` tree are rejected, and
parent-directory traversal is not accepted. `RUNE_R7_REPEATS` must be an
integer from 1 through 100 and is rejected before destination creation. The
bundle directory itself is created with fixed-provenance `mkdir` and must be
empty before metadata is written.

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
untracked changes cause a fail-closed exit before publication, so
`source_revision` identifies the exact contents built. The requested
`evidence/r7/...` path is absent during the final cleanliness checks and is
therefore not mistaken for a source mutation.
`assume-unchanged` and `skip-worktree` index flags are forbidden because
they can hide tracked modifications from ordinary status checks. Git routing
overrides are cleared and all provenance reads are rooted at the repository.
Git itself is resolved from a fixed provenance path
(`/usr/bin:/bin:/usr/sbin:/sbin`) and its absolute path/version are recorded;
repository-root discovery uses the fixed-provenance `dirname` rather than
ambient `PATH`;
the evidence build path is fixed and cannot substitute the Git/toolchain
executables. Git replacement objects are disabled, active replacement refs are
rejected, and tracked Makefile/source/include/study files are raw-hashed with
filters disabled and must match their blobs at `source_revision`. The actual evidence build does not compile those worktree files: it extracts
`Makefile`, `src/`, `include/`, and `study/` from
`git archive source_revision` into a per-capture snapshot. `TAR_OPTIONS` is
cleared before extraction and replay, unexpected snapshot files/symlinks are
rejected, then the expected snapshot bytes are verified and made read-only
before compiling. Ignored untracked files under
`src/`, `include/`, or `study/` are also forbidden because they can
satisfy compiler includes without appearing in ordinary status output.

For evidence capture, CC and AR must each identify one executable available
from the fixed provenance path. Their paths (and Make's path) are physically
canonicalized at the parent-directory level before trusted-prefix checks, so
lexical aliases such as `/usr/bin/../../tmp/wrapper` are rejected while normal
system symlinks remain usable. Arbitrary external compiler/archiver wrappers and
compound commands are rejected. Ambient compiler search variables (CPATH,
C_INCLUDE_PATH, CPLUS_INCLUDE_PATH, OBJC_INCLUDE_PATH, COMPILER_PATH,
LIBRARY_PATH, GCC_EXEC_PREFIX, and Clang's CCC_OVERRIDE_OPTIONS) are cleared;
intentional include paths belong in the recorded CPPFLAGS. Replay explicitly
clears the same override state before invoking the recorded compiler. Dynamic-loader injection/search variables
(`LD_PRELOAD`, loader library paths/auditing, relevant `DYLD_*` variables,
`LIBPATH`, and `SHLIB_PATH`) are cleared before toolchain identity and study
execution. CI exercises hostile compiler-search, Git-routing, ignored-header,
and loader-injection cases. Missing compiler identity, a failed/empty `uname -a` platform identity, or
missing memory profile causes the capture to fail. Platform and CPU-model
metadata are emitted with `printf` so shell `echo` escape handling cannot
merge or truncate required fields. The required `uname` platform probe, the `awk` memory parser, and optional
`getconf`/`sysctl` probes are resolved from the fixed provenance path rather
than ambient `PATH`. The getconf fallback requires both a nonzero numeric
physical page count and page size.

Inherited MAKEFLAGS/GNUMAKEFLAGS/MFLAGS/MAKEFILES/MAKEOVERRIDES are cleared for
the evidence build. Ambient `PATH` is replaced by the fixed recorded
`/usr/bin:/bin:/usr/sbin:/sbin` path; Make, compiler, archiver and build
utilities are resolved there. CPPFLAGS/CFLAGS metadata is emitted with
`printf`, preserving accepted backslashes exactly. Before Make runs, evidence
capture rejects Make-variable references, shell-evaluated substitution,
control, redirection, comment, tilde and pathname-globbing metacharacters,
`@` compiler response-file syntax, and Clang `--config...` configuration
file/search-directory controls. The recorded flag text therefore cannot turn
into unrecorded commands, response/config-file contents, or filesystem-dependent
argv during recipe/compiler evaluation. Each
capture uses one UTC-timestamp-plus-PID identity for its source snapshot,
`BUILD_DIR`, repository bundle stage, and sibling publish stage. The shared
repository `build/` parent is created idempotently, so concurrent captures are
isolated whether the parent already exists or is created by racing captures.
The build never relies on `make clean`, so a fake ambient `rm` cannot
preserve and certify a stale study executable.

The evidence build uses the Makefile extracted from the recorded Git revision
and invokes the resolved absolute Make path with
`-C <source-snapshot> -f Makefile`. The Make target
and `BUILD_DIR` passed into Make are repository-relative so repository paths
containing spaces remain parseable; the executable used by the shell remains
absolute. The bundle records the build-driver identity and Makefile Git blob
identity. Ignored
`GNUmakefile` or lowercase `makefile` files cannot replace the build recipe.
The recorded run command begins with `set -eu`, uses an absolute study
executable path, and includes the absolute redirection to `observations.tsv`.
A replay therefore stops on its first failed setup/build/study command instead
of allowing cleanup to mask the failure. Shell quoting is serialized
with a fixed-provenance `sed`, so paths containing apostrophes remain valid.
`command.txt` is replayable from outside the repository while recreating the
captured output artifact.

If measurement or finalization fails, an exit trap removes the staged bundle
and temporary source snapshot; no partial requested destination is retained.
Publication first copies the complete bundle to a hidden sibling directory on
the destination filesystem, verifies its SHA-256 manifest there, and only then
renames it into the immutable final path. Cross-filesystem ENOSPC therefore
cannot leave a partial requested destination.
This keeps the destination reusable after failures such as memory exhaustion.

Raw observations are execution evidence, not universal performance claims.
Interpretation belongs in a separately reviewed evidence commit.

CI includes dedicated deterministic regression gates in addition to the smoke
study: `tests/test_r7_run_local.sh` via `make r7-evidence-regression`, and
`tests/test_r7_clock.c` via `make r7-clock-regression` under UBSan. These
tests are the home for future reproductions affecting the evidence wrapper or
clock arithmetic. The evidence regression entrypoint works with plain
`make r7-evidence-regression` and Make's default `CC=cc`; its cleanup trap
also removes the shared-memory ENOSPC fixture on normal exit, failure, or
signal. CI timing values remain non-performance evidence.
