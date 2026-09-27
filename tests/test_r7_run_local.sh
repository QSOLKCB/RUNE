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
    rm -rf "$tmp_root" "$publish_parent"
    rm -f "/tmp/r7-reg-cc-$test_id" "/tmp/r7-reg-ar-$test_id" "/tmp/r7-reg-make-$test_id"
    rm -f "/tmp/r7-tar-hook-$test_id" "/tmp/r7-tar-hook-ran-$test_id"
    rm -f "/tmp/r7-ccc-header-$test_id.h"
    rm -f "/tmp/r7-backtick-header-$test_id.h" "/tmp/r7-backtick-hook-$test_id" "/tmp/r7-backtick-ran-$test_id"
    rm -f "/tmp/r7-separator-hook-$test_id" "/tmp/r7-separator-ran-$test_id"
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

rm -f "$backtick_header" "$backtick_hook" "$backtick_ran" "$separator_hook" "$separator_ran"

# Environment serialization must preserve backslashes and field boundaries.
scripts/r7-run-local.sh --self-test-environment-serialization

# Clang ambient option overrides must be cleared by capture and replay.
CCC_OVERRIDE_OPTIONS='#+-include +/tmp/should-not-exist.h' \
    scripts/r7-run-local.sh --self-test-compiler-search-env

case "$system_cc" in
    *clang)
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

# Replay must fail immediately and preserve observations when snapshot creation fails.
replay_bundle="$tmp_root/replay"
RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar"     scripts/r7-run-local.sh "$replay_bundle" >/dev/null
snapshot_path=$(/usr/bin/sed -n 's/^source_snapshot=//p' "$replay_bundle/environment.txt")
[ -n "$snapshot_path" ] || fail "replay bundle did not record source_snapshot"
before_hash=$(/usr/bin/sha256sum "$replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
printf 'block replay snapshot\n' > "$snapshot_path"
if (cd /tmp && /bin/sh "$replay_bundle/command.txt" >/dev/null 2>&1); then
    fail "replay ignored an early snapshot-creation failure"
fi
after_hash=$(/usr/bin/sha256sum "$replay_bundle/observations.tsv" | /usr/bin/awk '{print $1}')
[ "$before_hash" = "$after_hash" ] ||
    fail "failed replay modified observations.tsv"
rm -f "$snapshot_path"

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
