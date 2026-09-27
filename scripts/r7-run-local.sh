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
    export GIT_NO_REPLACE_OBJECTS=1

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

provenance_path=/usr/bin:/bin:/usr/sbin:/sbin
build_path=${RUNE_R7_BUILD_PATH:-/usr/bin:/bin:/usr/sbin:/sbin}

resolve_provenance_tool()
{
    PATH="$provenance_path" command -v "$1"
}

validate_build_path()
{
    case "$build_path" in
        ''|:*|*:|*::*)
            fail "RUNE_R7_BUILD_PATH must contain nonempty absolute directory entries"
            ;;
    esac

    old_ifs=$IFS
    IFS=:
    for path_entry in $build_path; do
        case "$path_entry" in
            /*) ;;
            *)
                IFS=$old_ifs
                fail "RUNE_R7_BUILD_PATH entries must be nonempty absolute directories"
                ;;
        esac
    done
    IFS=$old_ifs
}

resolve_build_tool()
{
    PATH="$build_path" command -v "$1"
}

validate_build_path

git_path=$(resolve_provenance_tool git) ||
    fail "Git not found in fixed provenance path"
case "$git_path" in
    /*) ;;
    *) fail "Git provenance path is not absolute: $git_path" ;;
esac
[ -x "$git_path" ] ||
    fail "Git provenance path is not executable: $git_path"
git_version=$("$git_path" --version 2>&1) ||
    fail "Git provenance identity command failed: $git_path --version"
[ -n "$git_version" ] ||
    fail "Git provenance identity output is empty"

if [ "${1:-}" = "--self-test-build-tools" ]; then
    for tool in make mkdir rm; do
        tool_path=$(resolve_build_tool "$tool") ||
            fail "required build tool not found in sanitized build path: $tool"
        case "$tool_path" in
            /*) ;;
            *) fail "build tool did not resolve to an absolute path: $tool_path" ;;
        esac
        printf '%s=%s\n' "$tool" "$tool_path"
    done
    exit 0
fi

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
    [ "${GIT_NO_REPLACE_OBJECTS:-}" = "1" ] ||
        fail "Git replacement objects are not disabled"
    case "$git_path" in
        /usr/bin/*|/bin/*|/usr/sbin/*|/sbin/*) ;;
        *) fail "Git provenance executable escaped fixed provenance path: $git_path" ;;
    esac
    git_root=$("$git_path" -C "$repo_root" rev-parse --show-toplevel) ||
        fail "Git root self-test could not resolve repository"
    [ "$git_root" = "$repo_root" ] ||
        fail "Git root self-test resolved foreign repository: $git_root"
    exit 0
fi

cc_name=${CC:-cc}
ar_name=${AR:-ar}
make_name=${MAKE:-make}
fresh_build_self_test=false
if [ "${1:-}" = "--self-test-fresh-build" ]; then
    fresh_build_self_test=true
fi
cppflags=${CPPFLAGS:-}
cflags=${CFLAGS:-}
repeats=${RUNE_R7_REPEATS:-5}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
out_dir=${1:-"evidence/r7/local-$stamp"}
out_parent=$(dirname -- "$out_dir")

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

case "$make_name" in
    *[[:space:]]*)
        fail "MAKE must name one build-driver executable"
        ;;
esac

case "$out_dir" in
    *"/../"*|"../"*|*/..|..)
        fail "evidence destination must not contain parent-directory traversal: $out_dir"
        ;;
esac

out_route=$out_dir
while [ "${out_route#./}" != "$out_route" ]; do
    out_route=${out_route#./}
done

case "$out_route" in
    build|build/*|"$repo_root/build"|"$repo_root/build/"*)
        fail "evidence destination must not route through build/: $out_dir"
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

replacement_refs=$("$git_path" -C "$repo_root" for-each-ref --format='%(refname)' refs/replace) ||
    fail "could not inspect Git replacement refs"

[ -z "$replacement_refs" ] ||
    fail "Git replacement refs are forbidden during evidence capture: $replacement_refs"

revision=$("$git_path" -C "$repo_root" rev-parse HEAD) ||
    fail "could not resolve source revision"

"$git_path" -C "$repo_root" ls-files --error-unmatch Makefile >/dev/null 2>&1 ||
    fail "tracked Makefile is missing"
makefile_blob=$("$git_path" -C "$repo_root" rev-parse "$revision:Makefile") ||
    fail "could not resolve tracked Makefile at source revision"
[ -n "$makefile_blob" ] ||
    fail "tracked Makefile identity is empty"

index_hidden=$("$git_path" -C "$repo_root" ls-files -v | awk '
    /^[a-z]/ || /^S / { print; exit }
') || fail "could not inspect Git index visibility flags"

[ -z "$index_hidden" ] ||
    fail "tracked files use assume-unchanged or skip-worktree flags; clear them before evidence capture"

ignored_inputs=$("$git_path" -C "$repo_root" ls-files --others --ignored --exclude-standard -- src include study) ||
    fail "could not inspect ignored compiler-input trees"

[ -z "$ignored_inputs" ] ||
    fail "ignored untracked files exist under compiler-input trees; remove them before evidence capture: $ignored_inputs"

git_status=$("$git_path" -C "$repo_root" status --porcelain --untracked-files=all) ||
    fail "could not inspect Git working-tree state"

[ -z "$git_status" ] ||
    fail "working tree is dirty; commit/stash tracked and untracked changes before evidence capture"

tracked_inputs=$("$git_path" -C "$repo_root" ls-files -- Makefile src include study) ||
    fail "could not enumerate tracked compiler inputs"

[ -n "$tracked_inputs" ] ||
    fail "tracked compiler-input set is empty"

while IFS= read -r tracked_path; do
    [ -n "$tracked_path" ] || continue
    [ -f "$repo_root/$tracked_path" ] ||
        fail "tracked compiler input is missing from worktree: $tracked_path"
    worktree_blob=$("$git_path" -C "$repo_root" hash-object --no-filters -- "$tracked_path") ||
        fail "could not hash raw worktree bytes: $tracked_path"
    revision_blob=$("$git_path" -C "$repo_root" rev-parse "$revision:$tracked_path") ||
        fail "could not resolve revision blob: $tracked_path"
    [ "$worktree_blob" = "$revision_blob" ] ||
        fail "raw worktree bytes differ from source revision: $tracked_path"
done <<R7_TRACKED_INPUTS
$tracked_inputs
R7_TRACKED_INPUTS

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

make_path=$(resolve_build_tool "$make_name") ||
    fail "build driver not found in sanitized build path: $make_name"
case "$make_path" in
    /*) ;;
    *) fail "build-driver path is not absolute: $make_path" ;;
esac
[ -x "$make_path" ] ||
    fail "build-driver path is not executable: $make_path"

if make_version=$("$make_path" --version 2>&1); then
    :
elif make_version=$("$make_path" -V MAKE_VERSION 2>&1); then
    :
else
    fail "build-driver identity command failed: $make_path"
fi
[ -n "$make_version" ] ||
    fail "build-driver identity output is empty"

mkdir_path=$(resolve_build_tool mkdir) ||
    fail "mkdir not found in sanitized build path"
rm_path=$(resolve_build_tool rm) ||
    fail "rm not found in sanitized build path"
case "$mkdir_path:$rm_path" in
    /*:/*) ;;
    *) fail "build utility paths must be absolute" ;;
esac

build_study()
{
    PATH="$build_path" MAKEFLAGS= GNUMAKEFLAGS= MFLAGS= MAKEFILES= MAKEOVERRIDES= \
    "$make_path" -C "$repo_root" -f Makefile "$study_target" \
        BUILD_DIR="$build_dir_rel" \
        CC="$cc_path" \
        AR="$ar_path" \
        CPPFLAGS="$cppflags" \
        CFLAGS="$cflags"
}

cleanup_build()
{
    "$rm_path" -rf "$build_dir" ||
        fail "could not remove fresh evidence build directory: $build_dir"
    [ ! -e "$build_dir" ] && [ ! -L "$build_dir" ] ||
        fail "fresh evidence build directory remains after cleanup: $build_dir"
}

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

build_dir_rel="build/r7-evidence-$stamp-$$"
build_dir="$repo_root/$build_dir_rel"
study_target="$build_dir_rel/rune_r7_study"
study_executable="$repo_root/$study_target"

if [ -e "$build_dir" ] || [ -L "$build_dir" ]; then
    fail "fresh evidence build directory already exists: $build_dir"
fi

if [ "$fresh_build_self_test" = true ]; then
    build_study
    "$study_executable" --bytes 32768 --repeats 1 >/dev/null
    cleanup_build
    exit 0
fi

mkdir "$out_dir" ||
    fail "could not create immutable evidence destination: $out_dir"

observations_path="$out_abs/observations.tsv"

{
    echo "contract=rune.r7.environment.v1"
    echo "evidence_class=raw-local-execution-observation"
    printf 'source_revision=%s\n' "$revision"
    printf 'working_tree_dirty=%s\n' "$dirty"
    printf '%s\n' "measurement_method=C99_clock_process_cpu_time"
    printf '%s\n' "benchmark_contract=rune.r7.memory-wall-observation.v1"
    printf 'makefile_path=%s\n' "$repo_root/Makefile"
    printf 'makefile_blob=%s\n' "$makefile_blob"
    printf 'repeats=%s\n' "$repeats"
    printf 'git_provenance_path=%s\n' "$provenance_path"
    printf 'git_resolved=%s\n' "$git_path"
    printf 'cc_requested=%s\n' "$cc_name"
    printf 'cc_resolved=%s\n' "$cc_path"
    printf 'ar_requested=%s\n' "$ar_name"
    printf 'ar_resolved=%s\n' "$ar_path"
    printf 'make_requested=%s\n' "$make_name"
    printf 'make_resolved=%s\n' "$make_path"
    printf 'build_path=%s\n' "$build_path"
    printf 'mkdir_resolved=%s\n' "$mkdir_path"
    printf 'rm_resolved=%s\n' "$rm_path"
    printf 'fresh_build_dir_relative=%s\n' "$build_dir_rel"
    printf 'fresh_build_dir=%s\n' "$build_dir"
    printf 'fresh_study_target=%s\n' "$study_target"
    printf 'cppflags=%s\n' "$cppflags"
    printf 'cflags=%s\n' "$cflags"
    echo "git_routing_environment=GIT_DIR,GIT_WORK_TREE,GIT_INDEX_FILE,GIT_OBJECT_DIRECTORY,GIT_ALTERNATE_OBJECT_DIRECTORIES,GIT_COMMON_DIR,GIT_NAMESPACE cleared"
    echo "git_replace_objects=disabled and replacement refs forbidden"
    echo "raw_worktree_identity=tracked Makefile/src/include/study hashed with git hash-object --no-filters"
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
    printf 'git_path=%s\n' "$git_path"
    printf '%s\n' "$git_version"
    echo "compiler_path=$cc_path"
    printf '%s\n' "$compiler_version"
    echo "archiver_path=$ar_path"
    printf '%s\n' "$archiver_version"
    echo "build_driver_path=$make_path"
    printf '%s\n' "$make_version"
} > "$out_dir/compiler.txt"

{
    printf "unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_COMMON_DIR GIT_NAMESPACE\n"
    printf "export GIT_NO_REPLACE_OBJECTS=1\n"
    printf "unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH COMPILER_PATH LIBRARY_PATH GCC_EXEC_PREFIX\n"
    printf "unset LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT DYLD_INSERT_LIBRARIES DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FALLBACK_FRAMEWORK_PATH LIBPATH SHLIB_PATH\n"
    printf "PATH="
    shell_quote "$build_path"
    printf " MAKEFLAGS='' GNUMAKEFLAGS='' MFLAGS='' MAKEFILES='' MAKEOVERRIDES='' "
    shell_quote "$make_path"
    printf " -C "
    shell_quote "$repo_root"
    printf " -f Makefile "
    shell_quote "$study_target"
    printf " BUILD_DIR="
    shell_quote "$build_dir_rel"
    printf " CC="
    shell_quote "$cc_path"
    printf " AR="
    shell_quote "$ar_path"
    printf " CPPFLAGS="
    shell_quote "$cppflags"
    printf " CFLAGS="
    shell_quote "$cflags"
    printf '\n'

    shell_quote "$study_executable"
    printf " --profile local --repeats "
    shell_quote "$repeats"
    printf " > "
    shell_quote "$observations_path"
    printf '\n'
} > "$out_dir/command.txt"

build_study

"$study_executable" --profile local --repeats "$repeats" \
    > "$observations_path"

cleanup_build

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
