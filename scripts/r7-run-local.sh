#!/bin/sh
set -eu

fail()
{
    echo "R7 evidence capture: $*" >&2
    exit 1
}

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_root"

cc_name=${CC:-cc}
cppflags=${CPPFLAGS:-}
cflags=${CFLAGS:-}
repeats=${RUNE_R7_REPEATS:-5}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
out_dir=${1:-"evidence/r7/local-$stamp"}
out_parent=$(dirname "$out_dir")

case "$cc_name" in
    *[[:space:]]*)
        fail "CC must name one compiler executable; put flags in CPPFLAGS/CFLAGS"
        ;;
esac

[ -d "$out_parent" ] ||
    fail "bundle parent directory does not exist: $out_parent"

if [ -e "$out_dir" ] || [ -L "$out_dir" ]; then
    fail "evidence destination already exists; refusing overwrite: $out_dir"
fi

revision=$(git rev-parse HEAD) ||
    fail "could not resolve source revision"

git_status=$(git status --porcelain --untracked-files=all) ||
    fail "could not inspect Git working-tree state"

if [ -n "$git_status" ]; then
    dirty=true
else
    dirty=false
fi

cc_path=$(command -v "$cc_name") ||
    fail "compiler not found: $cc_name"
[ -n "$cc_path" ] && [ -x "$cc_path" ] ||
    fail "compiler path is not executable: $cc_path"

compiler_version=$("$cc_path" --version 2>&1) ||
    fail "compiler identity command failed: $cc_path --version"
[ -n "$compiler_version" ] ||
    fail "compiler identity output is empty"

memory_profile=
if [ -r /proc/meminfo ]; then
    mem_total_kib=$(awk '/^MemTotal:/ { print $2; exit }' /proc/meminfo)
    case "$mem_total_kib" in
        ''|*[!0-9]*) ;;
        *) memory_profile="mem_total_kib=$mem_total_kib" ;;
    esac
fi

if [ -z "$memory_profile" ] && command -v sysctl >/dev/null 2>&1; then
    for key in hw.memsize hw.physmem64 hw.physmem; do
        value=$(sysctl -n "$key" 2>/dev/null || :)
        case "$value" in
            ''|*[!0-9]*) ;;
            *)
                memory_profile="mem_total_bytes=$value"
                break
                ;;
        esac
    done
fi

if [ -z "$memory_profile" ] && command -v getconf >/dev/null 2>&1; then
    phys_pages=$(getconf _PHYS_PAGES 2>/dev/null || :)
    page_size=$(getconf PAGE_SIZE 2>/dev/null || :)
    case "$phys_pages:$page_size" in
        *[!0-9:]*|:|*:0|0:*) ;;
        *)
            memory_profile="mem_phys_pages=$phys_pages
mem_page_size_bytes=$page_size"
            ;;
    esac
fi

[ -n "$memory_profile" ] ||
    fail "could not capture required memory profile"

cpu_model=
if [ -r /proc/cpuinfo ]; then
    cpu_model=$(awk -F ': ' '
        /^model name[[:space:]]*:/ { print $2; exit }
        /^Hardware[[:space:]]*:/ { print $2; exit }
    ' /proc/cpuinfo)
fi
if [ -z "$cpu_model" ] && command -v sysctl >/dev/null 2>&1; then
    for key in machdep.cpu.brand_string hw.model; do
        value=$(sysctl -n "$key" 2>/dev/null || :)
        if [ -n "$value" ]; then
            cpu_model=$value
            break
        fi
    done
fi

mkdir "$out_dir" ||
    fail "could not create immutable evidence destination: $out_dir"

{
    echo "contract=rune.r7.environment.v1"
    echo "evidence_class=raw-local-execution-observation"
    echo "source_revision=$revision"
    echo "working_tree_dirty=$dirty"
    echo "measurement_method=C99_clock_process_cpu_time"
    echo "benchmark_contract=rune.r7.memory-wall-observation.v1"
    echo "repeats=$repeats"
    echo "cc_requested=$cc_name"
    echo "cc_resolved=$cc_path"
    echo "cppflags=$cppflags"
    echo "cflags=$cflags"
    echo "uname=$(uname -a)"
    if command -v getconf >/dev/null 2>&1; then
        echo "processors_online=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo unknown)"
        echo "long_bit=$(getconf LONG_BIT 2>/dev/null || echo unknown)"
    fi
    if [ -n "$cpu_model" ]; then
        echo "cpu_model=$cpu_model"
    fi
    printf '%s\n' "$memory_profile"
} > "$out_dir/environment.txt"

{
    echo "compiler_path=$cc_path"
    printf '%s\n' "$compiler_version"
} > "$out_dir/compiler.txt"

{
    echo "make clean build/rune_r7_study CC=$cc_path CPPFLAGS=$cppflags CFLAGS=$cflags"
    echo "./build/rune_r7_study --profile local --repeats $repeats"
} > "$out_dir/command.txt"

make clean build/rune_r7_study     CC="$cc_path"     CPPFLAGS="$cppflags"     CFLAGS="$cflags"

./build/rune_r7_study --profile local --repeats "$repeats"     > "$out_dir/observations.tsv"

if command -v sha256sum >/dev/null 2>&1; then
    (
        cd "$out_dir"
        sha256sum             environment.txt             compiler.txt             command.txt             observations.tsv             > SHA256SUMS
    )
elif command -v shasum >/dev/null 2>&1; then
    (
        cd "$out_dir"
        shasum -a 256             environment.txt             compiler.txt             command.txt             observations.tsv             > SHA256SUMS
    )
else
    fail "no SHA-256 utility available"
fi

echo "R7 evidence bundle: $out_dir"
