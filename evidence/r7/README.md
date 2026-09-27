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

A bundle contains:

- `environment.txt` — source revision, dirty state, timer method, CPU-count and
  memory context where available;
- `compiler.txt` — compiler path/version output;
- `command.txt` — exact build/run commands;
- `observations.tsv` — raw numeric R7 observations;
- `SHA256SUMS` — integrity hashes for the bundle.

Raw observations are execution evidence, not universal performance claims.
Interpretation belongs in a separately reviewed evidence commit.

GitHub-hosted CI runs only `r7-study-smoke` to prove the harness builds,
executes, preserves comparison result identity, and emits the expected schema.
CI timing values are not R7 performance evidence.
