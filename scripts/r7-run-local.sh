#!/bin/sh
set -eu

fail()
{
    printf 'R7 evidence capture: %s\n' "$*" >&2
    exit 1
}

emit_key_value()
{
    printf '%s=%s\n' "$1" "$2"
}

shell_quote()
{
    printf "'"
    printf '%s' "$1" | "$sed_path" "s/'/'\\\\''/g"
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
    unset CCC_OVERRIDE_OPTIONS

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

    unset TAR_OPTIONS
}

sanitize_capture_environment

provenance_path=/usr/bin:/bin:/usr/sbin:/sbin
build_path=$provenance_path

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

canonicalize_existing_path()
{
    canonical_path=$1
    case "$canonical_path" in
        /*) ;;
        *) return 1 ;;
    esac

    canonical_parent=${canonical_path%/*}
    canonical_base=${canonical_path##*/}
    canonical_parent=$(CDPATH= cd -- "$canonical_parent" && pwd -P) || return 1
    printf '%s/%s\n' "$canonical_parent" "$canonical_base"
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

sed_path=$(resolve_provenance_tool sed) ||
    fail "sed not found in fixed provenance path"
awk_path=$(resolve_provenance_tool awk) ||
    fail "awk not found in fixed provenance path"
bundle_mkdir_path=$(resolve_provenance_tool mkdir) ||
    fail "mkdir not found in fixed provenance path"
tar_path=$(resolve_provenance_tool tar) ||
    fail "tar not found in fixed provenance path"
chmod_path=$(resolve_provenance_tool chmod) ||
    fail "chmod not found in fixed provenance path"
provenance_rm_path=$(resolve_provenance_tool rm) ||
    fail "rm not found in fixed provenance path"
mv_path=$(resolve_provenance_tool mv) ||
    fail "mv not found in fixed provenance path"
cp_path=$(resolve_provenance_tool cp) ||
    fail "cp not found in fixed provenance path"
find_path=$(resolve_provenance_tool find) ||
    fail "find not found in fixed provenance path"
readlink_path=$(resolve_provenance_tool readlink) ||
    fail "readlink not found in fixed provenance path"
date_path=$(resolve_provenance_tool date) ||
    fail "date not found in fixed provenance path"
dirname_path=$(resolve_provenance_tool dirname) ||
    fail "dirname not found in fixed provenance path"
basename_path=$(resolve_provenance_tool basename) ||
    fail "basename not found in fixed provenance path"
for provenance_tool_path in "$sed_path" "$awk_path" "$bundle_mkdir_path" "$tar_path" "$chmod_path" "$provenance_rm_path" "$mv_path" "$cp_path" "$find_path" "$readlink_path" "$date_path" "$dirname_path" "$basename_path"; do
    case "$provenance_tool_path" in
        /*) ;;
        *) fail "provenance tool path is not absolute: $provenance_tool_path" ;;
    esac
    [ -x "$provenance_tool_path" ] ||
        fail "provenance tool path is not executable: $provenance_tool_path"
done

getconf_path=$(resolve_provenance_tool getconf 2>/dev/null || :)
sysctl_path=$(resolve_provenance_tool sysctl 2>/dev/null || :)

uname_path=$(resolve_provenance_tool uname) ||
    fail "uname not found in fixed provenance path"
case "$uname_path" in
    /*) ;;
    *) fail "uname provenance path is not absolute: $uname_path" ;;
esac
[ -x "$uname_path" ] ||
    fail "uname provenance path is not executable: $uname_path"

hash_mode=
hash_path=$(resolve_provenance_tool sha256sum 2>/dev/null || :)
if [ -n "$hash_path" ]; then
    hash_mode=sha256sum
else
    hash_path=$(resolve_provenance_tool shasum 2>/dev/null || :)
    [ -n "$hash_path" ] || fail "no trusted SHA-256 utility available"
    hash_mode=shasum
fi
case "$hash_path" in
    /*) ;;
    *) fail "SHA-256 utility path is not absolute: $hash_path" ;;
esac
[ -x "$hash_path" ] ||
    fail "SHA-256 utility path is not executable: $hash_path"

if hash_version=$("$hash_path" --version 2>&1); then
    :
elif hash_version=$("$hash_path" -v 2>&1); then
    :
else
    fail "SHA-256 utility identity command failed: $hash_path"
fi
[ -n "$hash_version" ] ||
    fail "SHA-256 utility identity output is empty"

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
    [ -z "${CPATH+x}${C_INCLUDE_PATH+x}${CPLUS_INCLUDE_PATH+x}${OBJC_INCLUDE_PATH+x}${COMPILER_PATH+x}${LIBRARY_PATH+x}${GCC_EXEC_PREFIX+x}${CCC_OVERRIDE_OPTIONS+x}" ] ||
        fail "compiler search environment self-test failed"
    exit 0
fi

if [ "${1:-}" = "--self-test-environment-serialization" ]; then
    serialized=$(emit_key_value uname 'Linux r7\cprobe'; emit_key_value mem_total_kib 123)
    expected=$(printf '%s\n%s' 'uname=Linux r7\cprobe' 'mem_total_kib=123')
    [ "$serialized" = "$expected" ] ||
        fail "environment serialization self-test failed"
    exit 0
fi

if [ "${1:-}" = "--self-test-dynamic-loader-env" ]; then
    [ -z "${LD_PRELOAD+x}${LD_LIBRARY_PATH+x}${LD_AUDIT+x}${DYLD_INSERT_LIBRARIES+x}${DYLD_LIBRARY_PATH+x}${DYLD_FRAMEWORK_PATH+x}${DYLD_FALLBACK_LIBRARY_PATH+x}${DYLD_FALLBACK_FRAMEWORK_PATH+x}${LIBPATH+x}${SHLIB_PATH+x}" ] ||
        fail "dynamic-loader environment self-test failed"
    exit 0
fi

script_dir=$("$dirname_path" -- "$0") ||
    fail "could not resolve script directory"
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd -P) ||
    fail "could not resolve repository root"
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

validate_literal_build_flags()
{
    flag_name=$1
    flag_value=$2

    case "$flag_value" in
        *'$'*|*'`'*|*';'*|*'&'*|*'|'*|*'<'*|*'>'*|*'#'*|*'~'*|*'*'*|*'?'*|*'['*|*']'*|*'@'*)
            fail "$flag_name must not contain shell substitution, control, redirection, comment, tilde, glob, or compiler response-file metacharacters; use literal replayable compiler arguments"
            ;;
    esac
    case "$flag_value" in
        *'
'*|*''*)
            fail "$flag_name must not contain newline or carriage-return shell control characters"
            ;;
    esac
    case "$flag_value" in
        *'--config'*)
            fail "$flag_name must not contain Clang configuration-file controls; external compiler config files are not bound evidence inputs"
            ;;
    esac
}

validate_literal_build_flags CPPFLAGS "$cppflags"
validate_literal_build_flags CFLAGS "$cflags"

repeats=${RUNE_R7_REPEATS:-5}
case "$repeats" in
    ''|*[!0-9]*)
        fail "RUNE_R7_REPEATS must be an integer from 1 through 100"
        ;;
esac
normalized_repeats=$repeats
while [ "${normalized_repeats#0}" != "$normalized_repeats" ]; do
    normalized_repeats=${normalized_repeats#0}
done
[ -n "$normalized_repeats" ] ||
    fail "RUNE_R7_REPEATS must be an integer from 1 through 100"
case "$normalized_repeats" in
    [1-9]|[1-9][0-9]|100) ;;
    *) fail "RUNE_R7_REPEATS must be an integer from 1 through 100" ;;
esac
stamp=$("$date_path" -u +%Y%m%dT%H%M%SZ)
out_dir=${1:-"evidence/r7/local-$stamp"}
out_parent=$("$dirname_path" -- "$out_dir")

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
out_leaf=$("$basename_path" -- "$out_dir")
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

index_hidden=$("$git_path" -C "$repo_root" ls-files -v | "$awk_path" '
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

validate_source_identity()
{
    current_status=$("$git_path" -C "$repo_root" status --porcelain --untracked-files=all) ||
        fail "could not re-inspect Git working-tree state"
    [ -z "$current_status" ] ||
        fail "working tree changed during evidence capture"

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
}

validate_source_identity

validate_snapshot_source_entries()
{
    snapshot_entries=$("$find_path" "$source_snapshot" \( -type f -o -type l \) -print) ||
        fail "could not enumerate source snapshot entries"

    while IFS= read -r snapshot_entry; do
        [ -n "$snapshot_entry" ] || continue
        snapshot_rel=${snapshot_entry#"$source_snapshot"/}
        [ "$snapshot_rel" != "$snapshot_entry" ] ||
            fail "snapshot entry escaped snapshot root: $snapshot_entry"
        [ ! -L "$snapshot_entry" ] ||
            fail "source snapshot symlink is forbidden: $snapshot_rel"
        case "
$tracked_inputs
" in
            *"
$snapshot_rel
"*) ;;
            *) fail "unexpected source snapshot entry: $snapshot_rel" ;;
        esac
    done <<R7_SNAPSHOT_ENTRIES
$snapshot_entries
R7_SNAPSHOT_ENTRIES
}

validate_snapshot_identity()
{
    while IFS= read -r tracked_path; do
        [ -n "$tracked_path" ] || continue
        [ -f "$source_snapshot/$tracked_path" ] ||
            fail "snapshot compiler input is missing: $tracked_path"
        snapshot_blob=$("$git_path" -C "$repo_root" hash-object --no-filters -- "$source_snapshot/$tracked_path") ||
            fail "could not hash snapshot bytes: $tracked_path"
        revision_blob=$("$git_path" -C "$repo_root" rev-parse "$revision:$tracked_path") ||
            fail "could not resolve revision blob for snapshot: $tracked_path"
        [ "$snapshot_blob" = "$revision_blob" ] ||
            fail "snapshot bytes differ from source revision: $tracked_path"
    done <<R7_SNAPSHOT_INPUTS
$tracked_inputs
R7_SNAPSHOT_INPUTS
}

materialize_source_snapshot()
{
    if [ -L "$repo_root/build" ]; then
        fail "repository build path must not be a symlink"
    fi
    if [ -e "$repo_root/build" ] && [ ! -d "$repo_root/build" ]; then
        fail "repository build path is not a directory"
    fi
    "$bundle_mkdir_path" -p "$repo_root/build" ||
        fail "could not create repository build directory"
    [ -d "$repo_root/build" ] && [ ! -L "$repo_root/build" ] ||
        fail "repository build path changed during creation"

    [ ! -e "$source_snapshot" ] && [ ! -L "$source_snapshot" ] ||
        fail "source snapshot destination already exists: $source_snapshot"

    "$bundle_mkdir_path" "$source_snapshot" ||
        fail "could not create source snapshot directory: $source_snapshot"

    archive_path="$source_snapshot/.r7-source.tar"
    "$git_path" -C "$repo_root" archive --format=tar "$revision" -- Makefile src include study > "$archive_path" ||
        fail "could not materialize source archive for revision: $revision"
    "$tar_path" -xf "$archive_path" -C "$source_snapshot" ||
        fail "could not extract source snapshot"
    "$provenance_rm_path" -f "$archive_path" ||
        fail "could not remove temporary source archive"

    validate_snapshot_source_entries
    validate_snapshot_identity
    "$chmod_path" -R a-w         "$source_snapshot/Makefile"         "$source_snapshot/src"         "$source_snapshot/include"         "$source_snapshot/study" ||
        fail "could not make source snapshot read-only"
}

cleanup_snapshot()
{
    if [ -d "$source_snapshot" ]; then
        "$chmod_path" -R u+w "$source_snapshot" ||
            fail "could not restore snapshot write permission for cleanup"
    fi
    "$provenance_rm_path" -rf "$source_snapshot" ||
        fail "could not remove source snapshot: $source_snapshot"
    [ ! -e "$source_snapshot" ] && [ ! -L "$source_snapshot" ] ||
        fail "source snapshot remains after cleanup: $source_snapshot"
}

dirty=false

cc_path=$(resolve_provenance_tool "$cc_name") ||
    fail "compiler not found in fixed provenance path: $cc_name"
cc_path=$(canonicalize_existing_path "$cc_path") ||
    fail "could not canonicalize compiler path: $cc_path"
case "$cc_path" in
    /usr/bin/*|/bin/*|/usr/sbin/*|/sbin/*) ;;
    *) fail "compiler escaped fixed provenance path: $cc_path" ;;
esac
[ -n "$cc_path" ] && [ -x "$cc_path" ] ||
    fail "compiler path is not executable: $cc_path"

compiler_version=$("$cc_path" --version 2>&1) ||
    fail "compiler identity command failed: $cc_path --version"
[ -n "$compiler_version" ] ||
    fail "compiler identity output is empty"

ar_path=$(resolve_provenance_tool "$ar_name") ||
    fail "archiver not found in fixed provenance path: $ar_name"
ar_path=$(canonicalize_existing_path "$ar_path") ||
    fail "could not canonicalize archiver path: $ar_path"
case "$ar_path" in
    /usr/bin/*|/bin/*|/usr/sbin/*|/sbin/*) ;;
    *) fail "archiver escaped fixed provenance path: $ar_path" ;;
esac
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

make_path=$(resolve_provenance_tool "$make_name") ||
    fail "build driver not found in fixed provenance path: $make_name"
make_path=$(canonicalize_existing_path "$make_path") ||
    fail "could not canonicalize build-driver path: $make_path"
case "$make_path" in
    /usr/bin/*|/bin/*|/usr/sbin/*|/sbin/*) ;;
    *) fail "build-driver escaped fixed provenance path: $make_path" ;;
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

mkdir_path=$(resolve_provenance_tool mkdir) ||
    fail "mkdir not found in fixed provenance path"
rm_path=$(resolve_provenance_tool rm) ||
    fail "rm not found in fixed provenance path"
case "$mkdir_path:$rm_path" in
    /*:/*) ;;
    *) fail "build utility paths must be absolute" ;;
esac

build_study()
{
    PATH="$build_path" MAKEFLAGS= GNUMAKEFLAGS= MFLAGS= MAKEFILES= MAKEOVERRIDES= \
    "$make_path" -C "$source_snapshot" -f Makefile "$study_target" \
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
    mem_total_kib=$("$awk_path" '/^MemTotal:/ { print $2; exit }' /proc/meminfo)
    case "$mem_total_kib" in
        ''|*[!0-9]*) ;;
        *) memory_profile="mem_total_kib=$mem_total_kib" ;;
    esac
fi

if [ -z "$memory_profile" ] && [ -n "$sysctl_path" ]; then
    for key in hw.memsize hw.physmem64 hw.physmem; do
        value=$("$sysctl_path" -n "$key" 2>/dev/null || :)
        case "$value" in
            ''|*[!0-9]*) ;;
            *)
                memory_profile="mem_total_bytes=$value"
                break
                ;;
        esac
    done
fi

if [ -z "$memory_profile" ] && [ -n "$getconf_path" ]; then
    phys_pages=$("$getconf_path" _PHYS_PAGES 2>/dev/null || :)
    page_size=$("$getconf_path" PAGE_SIZE 2>/dev/null || :)

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

platform_identity=$("$uname_path" -a 2>&1) ||
    fail "platform identity command failed: $uname_path -a"
[ -n "$platform_identity" ] ||
    fail "platform identity output is empty"

cpu_model=
if [ -r /proc/cpuinfo ]; then
    cpu_model=$("$awk_path" -F ': ' '
        /^model name[[:space:]]*:/ { print $2; exit }
        /^Hardware[[:space:]]*:/ { print $2; exit }
    ' /proc/cpuinfo)
fi
if [ -z "$cpu_model" ] && [ -n "$sysctl_path" ]; then
    for key in machdep.cpu.brand_string hw.model; do
        value=$("$sysctl_path" -n "$key" 2>/dev/null || :)
        if [ -n "$value" ]; then
            cpu_model=$value
            break
        fi
    done
fi

capture_id="$stamp-$$"
source_snapshot_rel="build/r7-source-$capture_id"
source_snapshot="$repo_root/$source_snapshot_rel"
build_dir_rel="build/r7-evidence-$capture_id"
build_dir="$source_snapshot/$build_dir_rel"
study_target="$build_dir_rel/rune_r7_study"
study_executable="$source_snapshot/$study_target"
bundle_stage_rel="build/r7-bundle-stage-$capture_id"
bundle_stage="$repo_root/$bundle_stage_rel"
publish_stage="$out_parent_abs/.r7-publish-$capture_id"
case "$hash_mode" in
    sha256sum)
        publish_lock_id=$(printf '%s' "$out_abs" | "$hash_path" | "$awk_path" '{ print $1 }')
        ;;
    shasum)
        publish_lock_id=$(printf '%s' "$out_abs" | "$hash_path" -a 256 | "$awk_path" '{ print $1 }')
        ;;
    *)
        fail "unsupported SHA-256 utility mode for publication lock: $hash_mode"
        ;;
esac
case "$publish_lock_id" in
    ''|*[!0-9A-Fa-f]*)
        fail "could not derive destination-specific publication lock identity"
        ;;
esac
publish_lock="$out_parent_abs/.r7-publish-lock-$publish_lock_id"
publish_lock_held=false
cleanup_capture_state()
{
    if [ -n "${source_snapshot:-}" ] && [ -e "$source_snapshot" ]; then
        "$chmod_path" -R u+w "$source_snapshot" >/dev/null 2>&1 || :
        "$provenance_rm_path" -rf "$source_snapshot" >/dev/null 2>&1 || :
    fi
    if [ -n "${bundle_stage:-}" ] && [ -e "$bundle_stage" ]; then
        "$provenance_rm_path" -rf "$bundle_stage" >/dev/null 2>&1 || :
    fi
    if [ -n "${publish_stage:-}" ] && [ -e "$publish_stage" ]; then
        "$provenance_rm_path" -rf "$publish_stage" >/dev/null 2>&1 || :
    fi
    if [ "${publish_lock_held:-false}" = true ] &&
       [ -n "${publish_lock:-}" ] && [ -d "$publish_lock" ]; then
        "$provenance_rm_path" -rf "$publish_lock" >/dev/null 2>&1 || :
    fi
}

trap cleanup_capture_state 0 1 2 3 15

if [ "$fresh_build_self_test" = true ]; then
    materialize_source_snapshot
    build_study
    validate_snapshot_identity
    "$study_executable" --bytes 32768 --repeats 1 >/dev/null
    cleanup_snapshot
    exit 0
fi

materialize_source_snapshot
build_study
validate_snapshot_identity
validate_source_identity

[ ! -e "$bundle_stage" ] && [ ! -L "$bundle_stage" ] ||
    fail "bundle staging destination already exists: $bundle_stage"
"$bundle_mkdir_path" "$bundle_stage" ||
    fail "could not create bundle staging directory: $bundle_stage"

set -- "$bundle_stage"/* "$bundle_stage"/.[!.]* "$bundle_stage"/..?*
for bundle_entry in "$@"; do
    [ -e "$bundle_entry" ] || [ -L "$bundle_entry" ] || continue
    fail "new bundle staging directory is not empty: $bundle_stage"
done

observations_path="$bundle_stage/observations.tsv"

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
    printf 'sed_resolved=%s\n' "$sed_path"
    printf 'awk_resolved=%s\n' "$awk_path"
    printf 'bundle_mkdir_resolved=%s\n' "$bundle_mkdir_path"
    printf 'tar_resolved=%s\n' "$tar_path"
    printf 'chmod_resolved=%s\n' "$chmod_path"
    printf 'provenance_rm_resolved=%s\n' "$provenance_rm_path"
    printf 'mv_resolved=%s\n' "$mv_path"
    printf 'cp_resolved=%s\n' "$cp_path"
    printf 'find_resolved=%s\n' "$find_path"
    printf 'readlink_resolved=%s\n' "$readlink_path"
    printf 'bundle_stage=%s\n' "$bundle_stage"
    printf '%s\n' "bundle_publish_mode=publish_after_success_via_trusted_mv"
    printf '%s\n' "publish_lock_strategy=destination_specific_atomic_mkdir"
    printf 'date_resolved=%s\n' "$date_path"
    printf 'dirname_resolved=%s\n' "$dirname_path"
    printf 'basename_resolved=%s\n' "$basename_path"
    printf 'source_snapshot=%s\n' "$source_snapshot"
    printf 'source_snapshot_revision=%s\n' "$revision"
    printf '%s\n' "build_source_origin=git_archive_recorded_revision"
    printf '%s\n' "build_source_worktree_used=false"
    printf 'uname_resolved=%s\n' "$uname_path"
    printf 'sha256_mode=%s\n' "$hash_mode"
    printf 'sha256_resolved=%s\n' "$hash_path"
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
    echo "compiler_search_environment=CPATH,C_INCLUDE_PATH,CPLUS_INCLUDE_PATH,OBJC_INCLUDE_PATH,COMPILER_PATH,LIBRARY_PATH,GCC_EXEC_PREFIX,CCC_OVERRIDE_OPTIONS cleared"
    echo "dynamic_loader_environment=LD_PRELOAD,LD_LIBRARY_PATH,LD_AUDIT,DYLD_INSERT_LIBRARIES,DYLD_LIBRARY_PATH,DYLD_FRAMEWORK_PATH,DYLD_FALLBACK_LIBRARY_PATH,DYLD_FALLBACK_FRAMEWORK_PATH,LIBPATH,SHLIB_PATH cleared"
    echo "ignored_compiler_inputs=forbidden under src,include,study"
    echo "make_control_environment=MAKEFLAGS,GNUMAKEFLAGS,MFLAGS,MAKEFILES,MAKEOVERRIDES cleared"
    emit_key_value uname "$platform_identity"
    if [ -n "$getconf_path" ]; then
        processors_online=$("$getconf_path" _NPROCESSORS_ONLN 2>/dev/null || printf '%s' unknown)
        long_bit=$("$getconf_path" LONG_BIT 2>/dev/null || printf '%s' unknown)
        printf 'processors_online=%s\n' "$processors_online"
        printf 'long_bit=%s\n' "$long_bit"
    fi
    if [ -n "$cpu_model" ]; then
        emit_key_value cpu_model "$cpu_model"
    fi
    printf '%s\n' "$memory_profile"
} > "$bundle_stage/environment.txt"

{
    printf 'git_path=%s\n' "$git_path"
    printf '%s\n' "$git_version"
    printf 'sed_path=%s\n' "$sed_path"
    printf 'awk_path=%s\n' "$awk_path"
    printf 'bundle_mkdir_path=%s\n' "$bundle_mkdir_path"
    printf 'tar_path=%s\n' "$tar_path"
    printf 'chmod_path=%s\n' "$chmod_path"
    printf 'provenance_rm_path=%s\n' "$provenance_rm_path"
    printf 'mv_path=%s\n' "$mv_path"
    printf 'cp_path=%s\n' "$cp_path"
    printf 'find_path=%s\n' "$find_path"
    printf 'readlink_path=%s\n' "$readlink_path"
    printf 'date_path=%s\n' "$date_path"
    printf 'dirname_path=%s\n' "$dirname_path"
    printf 'basename_path=%s\n' "$basename_path"
    printf 'uname_path=%s\n' "$uname_path"
    printf 'sha256_path=%s\n' "$hash_path"
    printf '%s\n' "$hash_version"
    echo "compiler_path=$cc_path"
    printf '%s\n' "$compiler_version"
    echo "archiver_path=$ar_path"
    printf '%s\n' "$archiver_version"
    echo "build_driver_path=$make_path"
    printf '%s\n' "$make_version"
} > "$bundle_stage/compiler.txt"

{
    printf "set -eu\n"
    printf "unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_COMMON_DIR GIT_NAMESPACE\n"
    printf "export GIT_NO_REPLACE_OBJECTS=1\n"
    printf "unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH OBJC_INCLUDE_PATH COMPILER_PATH LIBRARY_PATH GCC_EXEC_PREFIX CCC_OVERRIDE_OPTIONS\n"
    printf "unset LD_PRELOAD LD_LIBRARY_PATH LD_AUDIT DYLD_INSERT_LIBRARIES DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH DYLD_FALLBACK_FRAMEWORK_PATH LIBPATH SHLIB_PATH\n"
    printf "unset TAR_OPTIONS\n"
    shell_quote "$bundle_mkdir_path"
    printf " -p "
    shell_quote "$repo_root/build"
    printf '\n'
    shell_quote "$bundle_mkdir_path"
    printf " "
    shell_quote "$source_snapshot"
    printf '\n'
    shell_quote "$git_path"
    printf " -C "
    shell_quote "$repo_root"
    printf " archive --format=tar "
    shell_quote "$revision"
    printf " -- Makefile src include study > "
    shell_quote "$source_snapshot/.r7-source.tar"
    printf '\n'
    shell_quote "$tar_path"
    printf " -xf "
    shell_quote "$source_snapshot/.r7-source.tar"
    printf " -C "
    shell_quote "$source_snapshot"
    printf '\n'
    shell_quote "$provenance_rm_path"
    printf " -f "
    shell_quote "$source_snapshot/.r7-source.tar"
    printf '\n'
    shell_quote "$chmod_path"
    printf " -R a-w "
    shell_quote "$source_snapshot/Makefile"
    printf " "
    shell_quote "$source_snapshot/src"
    printf " "
    shell_quote "$source_snapshot/include"
    printf " "
    shell_quote "$source_snapshot/study"
    printf '\n'
    printf "PATH="
    shell_quote "$build_path"
    printf " MAKEFLAGS='' GNUMAKEFLAGS='' MFLAGS='' MAKEFILES='' MAKEOVERRIDES='' "
    shell_quote "$make_path"
    printf " -C "
    shell_quote "$source_snapshot"
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
    shell_quote "$out_abs/observations.tsv"
    printf '\n'
    shell_quote "$chmod_path"
    printf " -R u+w "
    shell_quote "$source_snapshot"
    printf '\n'
    shell_quote "$provenance_rm_path"
    printf " -rf "
    shell_quote "$source_snapshot"
    printf '\n'
} > "$bundle_stage/command.txt"

"$study_executable" --profile local --repeats "$repeats" \
    > "$observations_path"

validate_snapshot_identity
validate_source_identity
cleanup_snapshot

case "$hash_mode" in
    sha256sum)
        (
            cd "$bundle_stage"
            "$hash_path" \
                environment.txt \
                compiler.txt \
                command.txt \
                observations.tsv \
                > SHA256SUMS
        )
        ;;
    shasum)
        (
            cd "$bundle_stage"
            "$hash_path" -a 256 \
                environment.txt \
                compiler.txt \
                command.txt \
                observations.tsv \
                > SHA256SUMS
        )
        ;;
    *)
        fail "unsupported SHA-256 utility mode: $hash_mode"
        ;;
esac

validate_source_identity

"$bundle_mkdir_path" "$publish_lock" ||
    fail "could not acquire destination publication lock; another capture may be publishing: $out_dir"
publish_lock_held=true

if [ -e "$out_dir" ] || [ -L "$out_dir" ]; then
    fail "evidence destination appeared during capture; refusing publish: $out_dir"
fi
[ ! -e "$publish_stage" ] && [ ! -L "$publish_stage" ] ||
    fail "publish staging destination already exists: $publish_stage"

"$bundle_mkdir_path" "$publish_stage" ||
    fail "could not create sibling publish staging directory"
"$cp_path" -R "$bundle_stage/." "$publish_stage/" ||
    fail "could not copy completed bundle to destination filesystem"

case "$hash_mode" in
    sha256sum)
        (
            cd "$publish_stage"
            "$hash_path" -c SHA256SUMS
        ) || fail "copied publish staging bundle failed SHA-256 verification"
        ;;
    shasum)
        (
            cd "$publish_stage"
            "$hash_path" -a 256 -c SHA256SUMS
        ) || fail "copied publish staging bundle failed SHA-256 verification"
        ;;
esac

"$provenance_rm_path" -rf "$bundle_stage" ||
    fail "could not remove repository bundle staging directory"

if [ -e "$out_abs" ] || [ -L "$out_abs" ]; then
    fail "evidence destination appeared during publish; refusing rename: $out_abs"
fi

"$mv_path" "$publish_stage" "$out_abs" ||
    fail "could not rename completed sibling staging bundle into place"
"$provenance_rm_path" -rf "$publish_lock" ||
    fail "published bundle but could not release destination publication lock"
publish_lock_held=false
trap - 0 1 2 3 15

echo "R7 evidence bundle: $out_dir"
