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
records and are never overwritten. Destinations resolving inside `build/` are
also rejected because evidence capture performs a clean rebuild.

A bundle contains:

- `environment.txt` — evidence class, source revision, dirty state including
  untracked files, timer method, exact CPPFLAGS/CFLAGS, CPU/platform context,
  and a required memory profile;
- `compiler.txt` — resolved compiler and archiver paths plus successful
  identity/version output;
- `command.txt` — exact replayable build/run commands using POSIX shell-safe
  quoting, including multiword values and embedded apostrophes;
- `observations.tsv` — raw numeric R7 observations;
- `SHA256SUMS` — integrity hashes for the bundle.

For evidence capture, CC and AR must each identify one executable; compound
commands are rejected. Missing compiler identity or memory profile causes the
capture to fail. The getconf fallback requires both a nonzero numeric physical
page count and page size.

Inherited MAKEFLAGS/GNUMAKEFLAGS/MFLAGS/MAKEFILES/MAKEOVERRIDES are cleared for the evidence build so a
dry-run flag such as MAKEFLAGS=-n cannot certify a stale executable.

Raw observations are execution evidence, not universal performance claims.
Interpretation belongs in a separately reviewed evidence commit.

GitHub-hosted CI runs only `r7-study-smoke` to prove the harness builds,
executes, preserves comparison result identity, and emits the expected schema.
CI timing values are not R7 performance evidence.
