#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

cc_name=${CC:-cc}
cflags=${CFLAGS:-}
repeats=${RUNE_R7_REPEATS:-5}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
out_dir=${1:-"evidence/r7/local-$stamp"}

mkdir -p "$out_dir"

revision=$(git rev-parse HEAD)
if git diff --quiet --ignore-submodules -- &&
   git diff --cached --quiet --ignore-submodules --; then
    dirty=false
else
    dirty=true
fi

{
    echo "contract=rune.r7.environment.v1"
    echo "source_revision=$revision"
    echo "working_tree_dirty=$dirty"
    echo "measurement_method=C99_clock_process_cpu_time"
    echo "benchmark_contract=rune.r7.memory-wall-observation.v1"
    echo "repeats=$repeats"
    echo "cc=$cc_name"
    echo "cflags=$cflags"
    echo "uname=$(uname -a)"
    if command -v getconf >/dev/null 2>&1; then
        echo "processors_online=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)"
        echo "long_bit=$(getconf LONG_BIT 2>/dev/null || echo unknown)"
    fi
    if [ -r /proc/cpuinfo ]; then
        awk -F ': ' '
            /^model name[[:space:]]*:/ { print "cpu_model=" $2; exit }
            /^Hardware[[:space:]]*:/ { print "cpu_model=" $2; exit }
        ' /proc/cpuinfo
    fi
    if [ -r /proc/meminfo ]; then
        awk '/^MemTotal:/ { print "mem_total_kib=" $2 }' /proc/meminfo
    fi
} > "$out_dir/environment.txt"

{
    command -v "$cc_name" 2>/dev/null || true
    "$cc_name" --version 2>&1 || true
} > "$out_dir/compiler.txt"

{
    echo "make clean build/rune_r7_study CC=$cc_name CFLAGS=$cflags"
    echo "./build/rune_r7_study --profile local --repeats $repeats"
} > "$out_dir/command.txt"

make clean build/rune_r7_study CC="$cc_name" CFLAGS="$cflags"
./build/rune_r7_study --profile local --repeats "$repeats"     > "$out_dir/observations.tsv"

if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "$out_dir"
        sha256sum environment.txt compiler.txt command.txt observations.tsv             > SHA256SUMS
    )
elif command -v shasum >/dev/null 2>&1; then
    (
        cd "$out_dir"
        shasum -a 256 environment.txt compiler.txt command.txt observations.tsv             > SHA256SUMS
    )
else
    echo "no SHA-256 utility available" >&2
    exit 1
fi

echo "R7 evidence bundle: $out_dir"
