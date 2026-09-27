#!/bin/sh
set -eu

fail()
{
    echo "R7 evidence capture: $*" >&2
    exit 1
}

shell_quote()
{
    printf "'"
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}

sanitize_capture_environment()
{
    unset GIT_DIR
    unset GIT_WORK_TREE
    unset GIT_INDEX_FILE
    unset GIT_OBJECT_DIRECTORY
    unset GIT_ALTERNATE_OBJECT_DIRECTORIES
    unset GIT_COMMON_DIR
    unset GIT_NAMESPACE

    unset CPATH
    unset C_INCLUDE_PATH
    unset CPLUS_INCLUDE_PATH
    unset OBJC_INCLUDE_PATH
    unset COMPILER_PATH
    unset LIBRARY_PATH
    unset GCC_EXEC_PREFIX

    unset LD_PRELOAD
    unset LD_LIBRARY_PATH
    unset LD_AUDIT
    unset DYLD_INSERT_LIBRARIES
    unset DYLD_LIBRARY_PATH
    unset DYLD_FRAMEWORK_PATH
    unset DYLD_FALLBACK_LIBRARY_PATH
    unset DYLD_FALLBACK_FRAMEWORK_PATH
    unset LIBPATH
    unset SHLIB_PATH
}

sanitize_capture_environment

if [ "${1:-}" = "--self-test-shell-quote" ]; then
    quoted=$(shell_quote "alpha beta'gamma")
    [ "$quoted" = "'alpha beta'\\''gamma'" ] ||
        fail "shell_quote self-test failed"
    exit 0
fi

if [ "${1:-}" = "--self-test-compiler-search-env" ]; then
    [ -z "${CPATH+x}${C_INCLUDE_PATH+x}${CPLUS_INCLUDE_PATH+x}${OBJC_INCLUDE_PATH+x}${COMPILER_PATH+x}${LIBRARY_PATH+x}${GCC_EXEC_PREFIX+x}" ] ||
        fail "compiler search environment self-test failed"
    exit 0
fi

if [ "${1:-}" = "--self-test-dynamic-loader-env" ]; then
    [ -z "${LD_PRELOAD+x}${LD_LIBRARY_PATH+x}${LD_AUDIT+x}${DYLD_INSERT_LIBRARIES+x}${DYLD_LIBRARY_PATH+x}${DYLD_FRAMEWORK_PATH+x}${DYLD_FALLBACK_LIBRARY_PATH+x}${DYLD_FALLBACK_FRAMEWORK_PATH+x}${LIBPATH+x}${SHLIB_PATH+x}" ] ||
        fail "dynamic-loader environment self-test failed"
    exit 0
fi

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
cd "$repo_root"

if [ "${1:-}" = "--self-test-git-root" ]; then
    git_root=$(git -C "$repo_root" rev-parse --show-toplevel) ||
        fail "Git root self-test could not resolve repository"
    [ "$git_root" = "$repo_root" ] ||
        fail "Git root self-test resolved foreign repository: $git_root"
    exit 0
fi

cc_name=${CC:-cc}
ar_name=${AR:-ar}
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

case "$ar_name" in
    *[[:space:]]*)
        fail "AR must name one archiver executable"
        ;;
esac

[ -d "$out_parent" ] ||
    fail "bundle parent directory does not exist: $out_parent"

out_parent_abs=$(CDPATH= cd -- "$out_parent" && pwd -P) ||
    fail "could not resolve bundle parent: $out_parent"
out_leaf=$(basename -- "$out_dir")
out_abs="$out_parent_abs/$out_leaf"

case "$out_abs" in
    "$repo_root/build"|"$repo_root/build/"*)
        fail "evidence destination must not be inside build/: $out_dir"
        ;;
esac

if [ -e "$out_dir" ] || [ -L "$out_dir" ]; then
    fail "evidence destination already exists; refusing overwrite: $out_dir"
fi

revision=$(git -C "$repo_root" rev-parse HEAD) ||
    fail "could not resolve source revision"

git -C "$repo_root" ls-files --error-unmatch Makefile >/dev/null 2>&1 ||
    fail "tracked Makefile is missing"
makefile_blob=$(git -C "$repo_root" rev-parse "$revision:Makefile") ||
    fail "could not resolve tracked Makefile at source revision"
[ -n "$makefile_blob" ] ||
    fail "tracked Makefile identity is empty"

index_hidden=$(git -C "$repo_root" ls-files -v | awk '
    /^[a-z]/ || /^S / { print; exit }
') || fail "could not inspect Git index visibility flags"

[ -z "$index_hidden" ] ||
    fail "tracked files use assume-unchanged or skip-worktree flags; clear them before evidence capture"

ignored_inputs=$(git -C "$repo_root" ls-files --others --ignored --exclude-standard -- src include study) ||
    fail "could not inspect ignored compiler-input trees"

[ -z "$ignored_inputs" ] ||
    fail "ignored untracked files exist under compiler-input trees; remove them before evidence capture: $ignored_inputs"

git_status=$(git -C "$repo_root" status --porcelain --untracked-files=all) ||
    fail "could not inspect Git working-tree state"

[ -z "$git_status" ] ||
    fail "working tree is dirty; commit/stash tracked and untracked changes before evidence capture"

dirty=false

cc_path=$(command -v "$cc_name") ||
    fail "compiler not found: $cc_name"
[ -n "$cc_path" ] && [ -x "$cc_path" ] ||
    fail "compiler path is not executable: $cc_path"

compiler_version=$("$cc_path" --version 2>&1) ||
    fail "compiler identity command failed: $cc_path --version"
[ -n "$compiler_version" ] ||
    fail "compiler identity output is empty"

ar_path=$(command -v "$ar_name") ||
    fail "archiver not found: $ar_name"
[ -n "$ar_path" ] && [ -x "$ar_path" ] ||
    fail "archiver path is not executable: $ar_path"

if archiver_version=$("$ar_path" --version 2>&1); then
    :
elif archiver_version=$("$ar_path" -V 2>&1); then
    :
else
    fail "archiver identity command failed: $ar_path"
fi
[ -n "$archiver_version" ] ||
    fail "archiver identity output is empty"

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

    case "$phys_pages" in
        ''|*[!0-9]*|0) phys_pages= ;;
    esac
    case "$page_size" in
        ''|*[!0-9]*|0) page_size= ;;
    esac

    if [ -n "$phys_pages" ] && [ -n "$page_size" ]; then
        memory_profile="mem_phys_pages=$phys_pages
mem_page_size_bytes=$page_size"
    fi
fi

[ -n "$memory_profile" ] ||
    fail "could not capture required memory profile"

platform_identity=$(uname -a 2>&1) ||
    fail "platform identity command failed: uname -a"
[ -n "$platform_identity" ] ||
    fail "platform identity output is empty"

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
    echo "makefile_path=$repo_root/Makefile"
    echo "makefile_blob=$makefile_blob"
    echo "repeats=$repeats"
    echo "cc_requested=$cc_name"
    echo "cc_resolved=$cc_path"
    echo "ar_requested=$ar_name"
    echo "ar_resolved=$ar_path"
    echo "cppflags=$cppflags"
    echo "cflags=$cflags"
    echo "git_routing_environment=GIT_DIR,GIT_WORK_TREE,GIT_INDEX_FILE,GIT_OBJECT_DIRECTORY,GIT_ALTERNATE_OBJECT_DIRECTORIES,GIT_COMMON_DIR,GIT_NAMESPACE cleared"
    echo "compiler_search_environment=CPATH,C_INCLUDE_PATH,CPLUS_INCLUDE_PATH,OBJC_INCLUDE_PATH,COMPILER_PATH,LIBRARY_PATH,GCC_EXEC_PREFIX cleared"
    echo "dynamic_loader_environment=LD_PRELOAD,LD_LIBRARY_PATH,LD_AUDIT,DYLD_INSERT_LIBRARIES,DYLD_LIBRARY_PATH,DYLD_FRAMEWORK_PATH,DYLD_FALLBACK_LIBRARY_PATH,DYLD_FALLBACK_FRAMEWORK_PATH,LIBPATH,SHLIB_PATH cleared"
    echo "ignored_compiler_inputs=forbidden under src,include,study"
    echo "make_control_environment=MAKEFLAGS,GNUMAKEFLAGS,MFLAGS,MAKEFILES,MAKEOVERRIDES cleared"
    echo "uname=$platform_identity"
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
    echo "archiver_path=$ar_path"
    printf '%s\n' "$archiver_version"
} > "$out_dir/compiler.txt"

{
    printf "unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_COMMON_DIR GIT_NAMESPACE\n"
    printf "unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH COMPILER_PATH LIBRARY_PATH GCC_EXEC_PREFIX\n"
    printf "unset LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT DYLD_INSERT_LIBRARIES DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FALLBACK_FRAMEWORK_PATH LIBPATH SHLIB_PATH\n"
    printf "MAKEFLAGS='' GNUMAKEFLAGS='' MFLAGS='' MAKEFILES='' MAKEOVERRIDES='' make -C "
    shell_quote "$repo_root"
    printf " -f Makefile clean build/rune_r7_study CC="
    shell_quote "$cc_path"
    printf " AR="
    shell_quote "$ar_path"
    printf " CPPFLAGS="
    shell_quote "$cppflags"
    printf " CFLAGS="
    shell_quote "$cflags"
    printf '\n'

    shell_quote "$repo_root/build/rune_r7_study"
    printf " --profile local --repeats "
    shell_quote "$repeats"
    printf '\n'
} > "$out_dir/command.txt"

MAKEFLAGS= GNUMAKEFLAGS= MFLAGS= MAKEFILES= MAKEOVERRIDES= \
make -C "$repo_root" -f Makefile clean build/rune_r7_study \
    CC="$cc_path" \
    AR="$ar_path" \
    CPPFLAGS="$cppflags" \
    CFLAGS="$cflags"

"$repo_root/build/rune_r7_study" --profile local --repeats "$repeats" \
    > "$out_dir/observations.tsv"

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
