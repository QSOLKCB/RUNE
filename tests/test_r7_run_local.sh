#!/bin/sh
set -eu

fail()
{
    echo "R7 evidence regression: $*" >&2
    exit 1
}

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
cd "$repo_root"

cc_name=${CC:-cc}
ar_name=${AR:-ar}

case "$cc_name" in
    cc|gcc|clang) system_cc="/usr/bin/$cc_name" ;;
    /usr/bin/cc|/usr/bin/gcc|/usr/bin/clang) system_cc=$cc_name ;;
    *) fail "R7 regression expects cc, gcc, or clang from /usr/bin: $cc_name" ;;
esac

case "$ar_name" in
    ar) system_ar=/usr/bin/ar ;;
    /usr/bin/ar) system_ar=$ar_name ;;
    *) fail "CI regression expects /usr/bin/ar: $ar_name" ;;
esac

test_id=$$
tmp_root="/tmp/rune-r7-evidence-regression-$test_id"
publish_parent="/dev/shm/r7-publish-regression-$test_id"
mkdir -p "$tmp_root"

cleanup()
{
    git config --local --unset-all core.worktree >/dev/null 2>&1 || :
    rm -f "$repo_root/r7-core-worktree-probe-$test_id"
    rm -rf "$tmp_root" "$publish_parent"
    rm -f "/tmp/r7-reg-cc-$test_id" "/tmp/r7-reg-ar-$test_id" "/tmp/r7-reg-make-$test_id"
    rm -f "/tmp/r7-tar-hook-$test_id" "/tmp/r7-tar-hook-ran-$test_id"
    rm -f "/tmp/r7-ccc-header-$test_id.h"
    rm -f "/tmp/r7-backtick-header-$test_id.h" "/tmp/r7-backtick-hook-$test_id" "/tmp/r7-backtick-ran-$test_id"
    rm -f "/tmp/r7-separator-hook-$test_id" "/tmp/r7-separator-ran-$test_id"
    rm -f "/tmp/r7-response-header-$test_id.h" "/tmp/r7-response-flags-$test_id.rsp"
    rm -f "/tmp/r7-clang-config-header-$test_id.h" "/tmp/r7-clang-config-$test_id.cfg"
    rm -f "/tmp/r7-gcc-spec-header-$test_id.h" "/tmp/r7-gcc-spec-$test_id.specs"
    rm -rf "/tmp/r7-bprefix-$test_id"
    rm -f "/tmp/r7-b-header-$test_id.h" "/tmp/r7-wrapper-header-$test_id.h" "/tmp/r7-wrapper-$test_id"
    rm -f "/tmp/r7-signal-$test_id.log" "/tmp/r7-prestudy-signal-$test_id.log"
    rm -f "/tmp/r7-concurrent-a-$test_id.log" "/tmp/r7-concurrent-b-$test_id.log"
    rm -f "/tmp/r7-compete-a-$test_id.log" "/tmp/r7-compete-b-$test_id.log"
    rm -rf "$repo_root"/build/r7-source-* "$repo_root"/build/r7-evidence-* "$repo_root"/build/r7-bundle-stage-*
}
trap cleanup 0 1 2 3 15

expect_capture_failure()
{
    label=$1
    shift
    dest="$tmp_root/$label"
    rm -rf "$dest"
    if "$@" "$dest" >/dev/null 2>&1; then
        fail "$label unexpectedly succeeded"
    fi
    [ ! -e "$dest" ] || fail "$label left a requested destination"
}

# Repository-root discovery must not consult ambient dirname.
fake_dirname_bin="$tmp_root/fake-dirname-bin"
mkdir -p "$fake_dirname_bin"
cat > "$fake_dirname_bin/dirname" <<'EOF'
#!/bin/sh
exit 97
EOF
chmod +x "$fake_dirname_bin/dirname"
PATH="$fake_dirname_bin:$PATH" scripts/r7-run-local.sh --self-test-git-root

# Repository-local core.worktree must not redirect provenance away from repo_root.
foreign_worktree="$tmp_root/foreign-worktree"
mkdir -p "$foreign_worktree"
core_worktree_probe="$repo_root/r7-core-worktree-probe-$test_id"
git config --local core.worktree "$foreign_worktree"
printf 'dirty actual repository\n' > "$core_worktree_probe"
core_worktree_dest="$tmp_root/core-worktree-redirect"
if RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh "$core_worktree_dest" >/dev/null 2>&1; then
    core_worktree_status=0
else
    core_worktree_status=$?
fi
git config --local --unset-all core.worktree
rm -f "$core_worktree_probe"
[ "$core_worktree_status" -ne 0 ] ||
    fail "core.worktree redirect allowed evidence capture from the wrong worktree"
[ ! -e "$core_worktree_dest" ] ||
    fail "core.worktree redirect published an evidence destination"

# Caller flags must be literal build configuration, never recursive Make references.
hidden_header="/tmp/r7-hidden-header-$test_id.h"
cat > "$hidden_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
expect_capture_failure make-reference-cppflags \
    env R7_HEADER="$hidden_header" 'CPPFLAGS=-include $(R7_HEADER)' \
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" scripts/r7-run-local.sh
expect_capture_failure make-reference-cflags \
    env R7_OPT='-O2' 'CFLAGS=$(R7_OPT)' \
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" scripts/r7-run-local.sh
rm -f "$hidden_header"

# Shell substitutions/control syntax in flags must be rejected before Make runs.
backtick_header="/tmp/r7-backtick-header-$test_id.h"
backtick_hook="/tmp/r7-backtick-hook-$test_id"
backtick_ran="/tmp/r7-backtick-ran-$test_id"
cat > "$backtick_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
cat > "$backtick_hook" <<EOF
#!/bin/sh
printf 'ran\n' > "$backtick_ran"
printf '%s\n' '-include $backtick_header'
EOF
chmod +x "$backtick_hook"
backtick_flags=$(printf '`%s`' "$backtick_hook")
expect_capture_failure shell-backtick-cflags \
    env "CFLAGS=$backtick_flags" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
[ ! -e "$backtick_ran" ] || fail "backtick CFLAGS hook executed before rejection"

separator_hook="/tmp/r7-separator-hook-$test_id"
separator_ran="/tmp/r7-separator-ran-$test_id"
cat > "$separator_hook" <<EOF
#!/bin/sh
printf 'ran\n' > "$separator_ran"
EOF
chmod +x "$separator_hook"
expect_capture_failure shell-separator-cflags \
    env "CFLAGS=-O2; $separator_hook" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
[ ! -e "$separator_ran" ] || fail "separator CFLAGS hook executed before rejection"

expect_capture_failure shell-glob-cppflags \
    env 'CPPFLAGS=-I/tmp/r7-*' RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure shell-brace-cflags \
    env 'CFLAGS=-{specs=/tmp/r7.specs,specs=/tmp/r7.specs}' \
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" scripts/r7-run-local.sh
expect_capture_failure shell-brace-cppflags \
    env 'CPPFLAGS=-I{/tmp/r7-a,/tmp/r7-b}' \
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" scripts/r7-run-local.sh

# Compiler response files must not hide effective build arguments.
response_header="/tmp/r7-response-header-$test_id.h"
response_flags="/tmp/r7-response-flags-$test_id.rsp"
cat > "$response_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
printf '%s\n' "-include $response_header" > "$response_flags"
expect_capture_failure compiler-response-cflags \
    env "CFLAGS=@$response_flags" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure compiler-response-cppflags \
    env "CPPFLAGS=@$response_flags" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh

# Clang configuration files and config search directories must not hide options.
clang_config_header="/tmp/r7-clang-config-header-$test_id.h"
clang_config="/tmp/r7-clang-config-$test_id.cfg"
cat > "$clang_config_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
printf '%s\n' "-include $clang_config_header" > "$clang_config"
expect_capture_failure clang-config-equals-cflags \
    env "CFLAGS=--config=$clang_config" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure clang-config-split-cppflags \
    env "CPPFLAGS=--config $clang_config" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure clang-config-system-dir \
    env 'CFLAGS=--config-system-dir=/tmp' RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure clang-config-user-dir \
    env 'CFLAGS=--config-user-dir=/tmp' RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh

# GCC specs files must not hide effective compiler arguments.
gcc_spec_header="/tmp/r7-gcc-spec-header-$test_id.h"
gcc_spec="/tmp/r7-gcc-spec-$test_id.specs"
cat > "$gcc_spec_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
cat > "$gcc_spec" <<EOF
*cpp:
-include $gcc_spec_header
EOF
expect_capture_failure gcc-specs-equals-cflags \
    env "CFLAGS=-specs=$gcc_spec" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure gcc-long-specs-equals-cppflags \
    env "CPPFLAGS=--specs=$gcc_spec" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure gcc-specs-split-cflags \
    env "CFLAGS=-specs $gcc_spec" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
tab_specs_flags=$(printf '%s\t%s' -specs "$gcc_spec")
expect_capture_failure gcc-specs-tab-cflags \
    env "CFLAGS=$tab_specs_flags" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh

# GCC -B must not replace trusted compiler subprograms with unbound executables.
b_prefix="/tmp/r7-bprefix-$test_id"
b_header="/tmp/r7-b-header-$test_id.h"
mkdir -p "$b_prefix"
cat > "$b_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 434343
EOF
real_cc1_path=$("$system_cc" -print-prog-name=cc1)
[ -n "$real_cc1_path" ] || fail "could not resolve compiler cc1 for -B regression"
cat > "$b_prefix/cc1" <<EOF
#!/bin/sh
exec "$real_cc1_path" -include "$b_header" "\$@"
EOF
chmod +x "$b_prefix/cc1"
expect_capture_failure gcc-b-split-cflags \
    env "CFLAGS=-B $b_prefix/" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure gcc-b-attached-cppflags \
    env "CPPFLAGS=-B$b_prefix/" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh

# GCC -wrapper must not introduce mutable subprocess wrappers.
wrapper_header="/tmp/r7-wrapper-header-$test_id.h"
wrapper_path="/tmp/r7-wrapper-$test_id"
cat > "$wrapper_header" <<'EOF'
#include <time.h>
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 454545
EOF
cat > "$wrapper_path" <<EOF
#!/bin/sh
program=\$1
shift
case "\${program##*/}" in
    cc1) exec "\$program" -include "$wrapper_header" "\$@" ;;
    *) exec "\$program" "\$@" ;;
esac
EOF
chmod +x "$wrapper_path"
expect_capture_failure gcc-wrapper-split-cflags \
    env "CFLAGS=-wrapper $wrapper_path" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh
expect_capture_failure gcc-wrapper-equals-cppflags \
    env "CPPFLAGS=-wrapper=$wrapper_path" RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh

rm -f "$backtick_header" "$backtick_hook" "$backtick_ran" "$separator_hook" "$separator_ran"
rm -f "$response_header" "$response_flags" "$clang_config_header" "$clang_config"
rm -f "$gcc_spec_header" "$gcc_spec"

# Perl interpreter injection/search variables must be cleared for shasum fallback use.
PERL5OPT=-MR7Hook PERL5LIB=/tmp/r7-perl-hook PERLLIB=/tmp/r7-perllib \
    PERL_UNICODE=S PERLIO=raw PERL_LOCAL_LIB_ROOT=/tmp/r7-local-lib \
    PERL_MB_OPT=--install_base=/tmp/r7-mb PERL_MM_OPT=INSTALL_BASE=/tmp/r7-mm \
    scripts/r7-run-local.sh --self-test-perl-hash-env

# Environment serialization must preserve backslashes and field boundaries.
scripts/r7-run-local.sh --self-test-environment-serialization

# Clang ambient option overrides must be cleared by capture and replay.
CCC_OVERRIDE_OPTIONS='#+-include +/tmp/should-not-exist.h' \
    scripts/r7-run-local.sh --self-test-compiler-search-env

case "$system_cc" in
    *clang)
        clang_default_bundle="$tmp_root/clang-default-config"
        RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
            scripts/r7-run-local.sh "$clang_default_bundle" >/dev/null
        /usr/bin/grep -Fxq 'compiler_default_config_control=--no-default-config' \
            "$clang_default_bundle/environment.txt" ||
            fail "Clang capture did not record --no-default-config isolation"
        /usr/bin/grep -Fxq 'compiler_default_config_control=--no-default-config' \
            "$clang_default_bundle/compiler.txt" ||
            fail "Clang compiler metadata did not bind --no-default-config"
        /usr/bin/grep -Fq "CPPFLAGS='--no-default-config" "$clang_default_bundle/command.txt" ||
            fail "Clang replay did not carry --no-default-config"

        cat > /tmp/r7-ccc-header-$test_id.h <<'EOF'
#undef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC 424242
EOF
        ccc_bundle="$tmp_root/ccc-override"
        CCC_OVERRIDE_OPTIONS="#+-include +/tmp/r7-ccc-header-$test_id.h" \
            RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
            scripts/r7-run-local.sh "$ccc_bundle" >/dev/null
        if /usr/bin/awk -F '\t' '$16 == 424242 { bad=1 } END { exit bad ? 0 : 1 }' "$ccc_bundle/observations.tsv"; then
            fail "CCC_OVERRIDE_OPTIONS changed captured clock scale"
        fi
        /usr/bin/grep -Fq 'CCC_OVERRIDE_OPTIONS' "$ccc_bundle/command.txt" ||
            fail "replay does not explicitly clear CCC_OVERRIDE_OPTIONS"
        CCC_OVERRIDE_OPTIONS="#+-include +/tmp/r7-ccc-header-$test_id.h" \
            /bin/sh "$ccc_bundle/command.txt" >/dev/null
        if /usr/bin/awk -F '\t' '$16 == 424242 { bad=1 } END { exit bad ? 0 : 1 }' "$ccc_bundle/observations.tsv"; then
            fail "CCC_OVERRIDE_OPTIONS changed replayed clock scale"
        fi
        ;;
esac

# SIGTERM before the study starts must stop the capture before measurement.
pre_signal_dest="$tmp_root/prestudy-signal"
pre_signal_log="/tmp/r7-prestudy-signal-$test_id.log"
RUNE_R7_REPEATS=100 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh "$pre_signal_dest" >"$pre_signal_log" 2>&1 &
pre_signal_pid=$!
pre_signal_source=
pre_signal_probe=0
while [ "$pre_signal_probe" -lt 100 ]; do
    pre_signal_source=$(/usr/bin/find build -maxdepth 1 -type d -name 'r7-source-*' -print -quit)
    [ -n "$pre_signal_source" ] && break
    if ! kill -0 "$pre_signal_pid" 2>/dev/null; then
        cat "$pre_signal_log" >&2
        fail "pre-study signal capture exited before source snapshot appeared"
    fi
    sleep 0.05
    pre_signal_probe=$((pre_signal_probe + 1))
done
[ -n "$pre_signal_source" ] || {
    kill -TERM "$pre_signal_pid" >/dev/null 2>&1 || :
    fail "could not observe source snapshot for pre-study signal regression"
}
kill -TERM "$pre_signal_pid"
pre_signal_probe=0
while kill -0 "$pre_signal_pid" 2>/dev/null && [ "$pre_signal_probe" -lt 100 ]; do
    sleep 0.05
    pre_signal_probe=$((pre_signal_probe + 1))
done
if kill -0 "$pre_signal_pid" 2>/dev/null; then
    kill -KILL "$pre_signal_pid" >/dev/null 2>&1 || :
    fail "pre-study SIGTERM did not terminate wrapper promptly"
fi
if wait "$pre_signal_pid"; then
    fail "pre-study signal capture unexpectedly exited successfully"
else
    pre_signal_status=$?
fi
[ "$pre_signal_status" -eq 143 ] ||
    fail "pre-study signal capture exited with status $pre_signal_status instead of 143"
[ ! -e "$pre_signal_dest" ] || fail "pre-study terminated capture published a destination"
[ ! -e "$pre_signal_source" ] ||
    fail "pre-study terminated capture left its source snapshot"

# SIGTERM to the wrapper must promptly terminate the active study and clean staging.
signal_dest="$tmp_root/signal-forward"
signal_log="/tmp/r7-signal-$test_id.log"
RUNE_R7_REPEATS=100 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh "$signal_dest" >"$signal_log" 2>&1 &
signal_wrapper_pid=$!
signal_stage=
signal_study_pid=
signal_probe=0
while [ "$signal_probe" -lt 30 ]; do
    signal_stage=$(/usr/bin/find build -maxdepth 1 -type d -name 'r7-bundle-stage-*' -print -quit)
    if [ -n "$signal_stage" ] && [ -e "$signal_stage/observations.tsv" ]; then
        signal_study_pid=$(
            /usr/bin/ps -eo pid=,ppid=,comm= |
                /usr/bin/awk -v parent="$signal_wrapper_pid"                     '$2 == parent && $3 ~ /rune_r7_study/ { print $1; exit }'
        )
        [ -n "$signal_study_pid" ] && break
    fi
    if ! kill -0 "$signal_wrapper_pid" 2>/dev/null; then
        cat "$signal_log" >&2
        fail "signal-forward capture exited before the active study was observed"
    fi
    sleep 1
    signal_probe=$((signal_probe + 1))
done
[ -n "$signal_study_pid" ] || {
    cat "$signal_log" >&2
    kill -TERM "$signal_wrapper_pid" >/dev/null 2>&1 || :
    fail "could not observe active R7 study for signal-forward regression"
}

kill -TERM "$signal_wrapper_pid"
signal_probe=0
while kill -0 "$signal_wrapper_pid" 2>/dev/null && [ "$signal_probe" -lt 10 ]; do
    sleep 1
    signal_probe=$((signal_probe + 1))
done
if kill -0 "$signal_wrapper_pid" 2>/dev/null; then
    kill -KILL "$signal_wrapper_pid" >/dev/null 2>&1 || :
    kill -KILL "$signal_study_pid" >/dev/null 2>&1 || :
    fail "SIGTERM was not forwarded promptly to the active R7 study"
fi
if wait "$signal_wrapper_pid"; then
    fail "signal-forward capture unexpectedly exited successfully"
else
    signal_status=$?
fi
[ "$signal_status" -eq 143 ] ||
    fail "signal-forward capture exited with status $signal_status instead of 143"
signal_probe=0
while kill -0 "$signal_study_pid" 2>/dev/null && [ "$signal_probe" -lt 5 ]; do
    sleep 1
    signal_probe=$((signal_probe + 1))
done
if kill -0 "$signal_study_pid" 2>/dev/null; then
    kill -KILL "$signal_study_pid" >/dev/null 2>&1 || :
    fail "R7 study child remained alive after wrapper SIGTERM"
fi
[ ! -e "$signal_dest" ] || fail "terminated capture published a destination"
[ -z "$signal_stage" ] || [ ! -e "$signal_stage" ] ||
    fail "terminated capture left its bundle staging directory"

run_concurrent_pair()
{
    label=$1
    dest_a="$tmp_root/$label-a"
    dest_b="$tmp_root/$label-b"
    rm -rf "$dest_a" "$dest_b"

    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
        scripts/r7-run-local.sh "$dest_a" > /tmp/r7-concurrent-a-$test_id.log 2>&1 &
    pid_a=$!
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
        scripts/r7-run-local.sh "$dest_b" > /tmp/r7-concurrent-b-$test_id.log 2>&1 &
    pid_b=$!

    if wait "$pid_a"; then status_a=0; else status_a=$?; fi
    if wait "$pid_b"; then status_b=0; else status_b=$?; fi

    [ "$status_a" -eq 0 ] || {
        cat /tmp/r7-concurrent-a-$test_id.log >&2
        fail "$label capture A failed with status $status_a"
    }
    [ "$status_b" -eq 0 ] || {
        cat /tmp/r7-concurrent-b-$test_id.log >&2
        fail "$label capture B failed with status $status_b"
    }

    for concurrent_bundle in "$dest_a" "$dest_b"; do
        [ -s "$concurrent_bundle/observations.tsv" ] ||
            fail "$label did not publish observations"
        (
            cd "$concurrent_bundle"
            /usr/bin/sha256sum -c SHA256SUMS >/dev/null
        )
    done

    if /usr/bin/find build -maxdepth 1 -type d \( -name 'r7-source-*' -o -name 'r7-bundle-stage-*' \) -print -quit | /usr/bin/grep -q .; then
        fail "$label leaked shared capture state"
    fi
}

run_competing_destination_pair()
{
    label=$1
    dest="$tmp_root/$label"
    rm -rf "$dest"

    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
        scripts/r7-run-local.sh "$dest" > /tmp/r7-compete-a-$test_id.log 2>&1 &
    pid_a=$!
    RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
        scripts/r7-run-local.sh "$dest" > /tmp/r7-compete-b-$test_id.log 2>&1 &
    pid_b=$!

    if wait "$pid_a"; then status_a=0; else status_a=$?; fi
    if wait "$pid_b"; then status_b=0; else status_b=$?; fi

    success_count=0
    if [ "$status_a" -eq 0 ]; then success_count=$((success_count + 1)); fi
    if [ "$status_b" -eq 0 ]; then success_count=$((success_count + 1)); fi
    if [ "$success_count" -ne 1 ]; then
        cat /tmp/r7-compete-a-$test_id.log >&2
        cat /tmp/r7-compete-b-$test_id.log >&2
        fail "$label expected exactly one successful publisher; got statuses $status_a and $status_b"
    fi

    [ -s "$dest/observations.tsv" ] ||
        fail "$label did not publish observations"
    (
        cd "$dest"
        /usr/bin/sha256sum -c SHA256SUMS >/dev/null
    )

    if /usr/bin/find "$dest" -maxdepth 1 -type d -name '.r7-publish-*' -print -quit | /usr/bin/grep -q .; then
        fail "$label nested a competing publish stage inside the immutable bundle"
    fi
    if /usr/bin/find "$tmp_root" -maxdepth 1 -type d -name '.r7-publish-lock-*' -print -quit | /usr/bin/grep -q .; then
        fail "$label leaked a destination publication lock"
    fi
}

# Concurrent captures must isolate all PID-qualified staging paths.
mkdir -p build
run_concurrent_pair concurrent-existing-build

# Shared build-parent creation must be race-safe from a clean absent build/.
rm -rf build
run_concurrent_pair concurrent-missing-build

# Competing captures for one absent destination must serialize final publication.
run_competing_destination_pair concurrent-same-destination

# Canonical-path regression: lexical /usr/bin prefixes must not authorize /tmp.
cat > /tmp/r7-reg-cc-$test_id <<EOF
#!/bin/sh
exec "$system_cc" "\$@"
EOF
cat > /tmp/r7-reg-ar-$test_id <<EOF
#!/bin/sh
exec "$system_ar" "\$@"
EOF
cat > /tmp/r7-reg-make-$test_id <<'EOF'
#!/bin/sh
exec /usr/bin/make "$@"
EOF
chmod +x /tmp/r7-reg-cc-$test_id /tmp/r7-reg-ar-$test_id /tmp/r7-reg-make-$test_id

expect_capture_failure canonical-cc     env RUNE_R7_REPEATS=1 CC="/usr/bin/../../tmp/r7-reg-cc-$test_id" AR="$system_ar"     scripts/r7-run-local.sh
expect_capture_failure canonical-ar     env RUNE_R7_REPEATS=1 CC="$system_cc" AR="/usr/bin/../../tmp/r7-reg-ar-$test_id"     scripts/r7-run-local.sh
expect_capture_failure canonical-make     env RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" MAKE="/usr/bin/../../tmp/r7-reg-make-$test_id"     scripts/r7-run-local.sh

# Replay must fail immediately and preserve observations when its PID-qualified
# snapshot path is blocked before command execution.
replay_bundle="$tmp_root/replay"
RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh "$replay_bundle" >/dev/null
snapshot_assignment=$(/usr/bin/grep -m1 '^replay_source_snapshot=' "$replay_bundle/command.txt")
case "$snapshot_assignment" in
    replay_source_snapshot=\'*\'\$\$) ;;
    *) fail "replay command missing PID-qualified replay_source_snapshot assignment" ;;
esac
snapshot_prefix=${snapshot_assignment#replay_source_snapshot=\'}
snapshot_prefix=${snapshot_prefix%\'\$\$}
[ -n "$snapshot_prefix" ] || fail "replay command recorded empty replay_source_snapshot prefix"
before_hash=$(/usr/bin/sha256sum "$replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
if (
    cd /tmp
    SNAPSHOT_PREFIX="$snapshot_prefix" REPLAY_COMMAND="$replay_bundle/command.txt" /bin/sh -c '
        blocker="${SNAPSHOT_PREFIX}$$"
        printf "%s\n" "block replay snapshot" > "$blocker"
        exec /bin/sh "$REPLAY_COMMAND"
    ' >/dev/null 2>&1
); then
    fail "replay ignored an early snapshot-creation failure"
fi
after_hash=$(/usr/bin/sha256sum "$replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
[ "$before_hash" = "$after_hash" ] ||
    fail "failed replay modified observations.tsv"
snapshot_parent=${snapshot_prefix%/*}
snapshot_leaf=${snapshot_prefix##*/}
if /usr/bin/find "$snapshot_parent" -maxdepth 1 -name "$snapshot_leaf*" -print -quit | /usr/bin/grep -q .; then
    fail "failed replay left its PID-qualified source snapshot"
fi

# Replay study failure must preserve checksum-bound observations and clean snapshot/temp output.
runtime_replay_bundle="$tmp_root/replay-runtime-failure"
RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" \
    scripts/r7-run-local.sh "$runtime_replay_bundle" >/dev/null
runtime_snapshot_assignment=$(/usr/bin/grep -m1 '^replay_source_snapshot=' "$runtime_replay_bundle/command.txt")
case "$runtime_snapshot_assignment" in
    replay_source_snapshot=\'*\'\$\$) ;;
    *) fail "runtime replay command missing PID-qualified replay_source_snapshot assignment" ;;
esac
runtime_snapshot_prefix=${runtime_snapshot_assignment#replay_source_snapshot=\'}
runtime_snapshot_prefix=${runtime_snapshot_prefix%\'\$\$}
[ -n "$runtime_snapshot_prefix" ] || fail "runtime replay command recorded empty replay_source_snapshot prefix"
runtime_before_hash=$(/usr/bin/sha256sum "$runtime_replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
if (ulimit -v 120000; /bin/sh "$runtime_replay_bundle/command.txt" >/dev/null 2>&1); then
    fail "resource-constrained replay unexpectedly succeeded"
fi
runtime_after_hash=$(/usr/bin/sha256sum "$runtime_replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
[ "$runtime_before_hash" = "$runtime_after_hash" ] ||
    fail "failed runtime replay modified observations.tsv"
runtime_snapshot_parent=${runtime_snapshot_prefix%/*}
runtime_snapshot_leaf=${runtime_snapshot_prefix##*/}
if /usr/bin/find "$runtime_snapshot_parent" -maxdepth 1 -name "$runtime_snapshot_leaf*" -print -quit | /usr/bin/grep -q .; then
    fail "failed runtime replay left its PID-qualified source snapshot"
fi
if /usr/bin/find "$runtime_replay_bundle" -maxdepth 1 -name '.r7-replay-observations-*' -print -quit | /usr/bin/grep -q .; then
    fail "failed runtime replay left temporary observations"
fi
(
    cd "$runtime_replay_bundle"
    /usr/bin/sha256sum -c SHA256SUMS >/dev/null
) || fail "failed runtime replay invalidated the evidence bundle"
# TAR_OPTIONS must be cleared for capture and replay; hook execution is forbidden.
cat > /tmp/r7-tar-hook-$test_id <<EOF
#!/bin/sh
printf 'ran\n' > /tmp/r7-tar-hook-ran-$test_id
EOF
chmod +x /tmp/r7-tar-hook-$test_id
tar_bundle="$tmp_root/tar"
TAR_OPTIONS="--checkpoint=1 --checkpoint-action=exec=/tmp/r7-tar-hook-$test_id"     RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar"     scripts/r7-run-local.sh "$tar_bundle" >/dev/null
[ ! -e /tmp/r7-tar-hook-ran-$test_id ] ||
    fail "TAR_OPTIONS hook executed during capture"
TAR_OPTIONS="--checkpoint=1 --checkpoint-action=exec=/tmp/r7-tar-hook-$test_id"     /bin/sh "$tar_bundle/command.txt" >/dev/null
[ ! -e /tmp/r7-tar-hook-ran-$test_id ] ||
    fail "TAR_OPTIONS hook executed during replay"
if /usr/bin/awk -F '\t' '$16 == 616161 { bad=1 } END { exit bad ? 0 : 1 }' "$tar_bundle/observations.tsv"; then
    fail "tar injection changed the study clock scale"
fi

# Failed destination-filesystem copy must never create the immutable final path.
[ -d /dev/shm ] && [ -w /dev/shm ] ||
    fail "/dev/shm is required for deterministic publication regression"
publish_dest="$publish_parent/evidence"
fill_file="$publish_parent/fill"
mkdir -p "$publish_parent"
available_kb=$(/usr/bin/df -Pk "$publish_parent" | /usr/bin/awk 'NR == 2 { print $4 }')
case "$available_kb" in
    ''|*[!0-9]*) fail "could not determine /dev/shm free space" ;;
esac
[ "$available_kb" -gt 16 ] ||
    fail "/dev/shm has insufficient free space for publication regression"
fill_kb=$((available_kb - 8))
/usr/bin/fallocate -l "$((fill_kb * 1024))" "$fill_file"
if RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar"     scripts/r7-run-local.sh "$publish_dest" >/dev/null 2>&1; then
    rm -f "$fill_file"
    rm -rf "$publish_parent"
    fail "nearly-full destination filesystem unexpectedly accepted publication"
fi
[ ! -e "$publish_dest" ] ||
    fail "failed publication left the immutable destination behind"
if /usr/bin/find "$publish_parent" -maxdepth 1 -name '.r7-publish-*' -print -quit | /usr/bin/grep -q .; then
    fail "failed publication left a sibling staging directory"
fi
rm -f "$fill_file"
rm -rf "$publish_parent"

# No capture regression may leak internal source/staging directories.
if /usr/bin/find build -maxdepth 1 -type d \( -name 'r7-source-*' -o -name 'r7-bundle-stage-*' \) -print -quit | /usr/bin/grep -q .; then
    fail "R7 evidence regression leaked internal capture state"
fi

echo "R7 evidence regressions: OK"
