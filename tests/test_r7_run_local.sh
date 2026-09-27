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
    gcc|clang) system_cc="/usr/bin/$cc_name" ;;
    /usr/bin/gcc|/usr/bin/clang) system_cc=$cc_name ;;
    *) fail "CI regression expects gcc or clang from /usr/bin: $cc_name" ;;
esac

case "$ar_name" in
    ar) system_ar=/usr/bin/ar ;;
    /usr/bin/ar) system_ar=$ar_name ;;
    *) fail "CI regression expects /usr/bin/ar: $ar_name" ;;
esac

tmp_root="/tmp/rune-r7-evidence-regression-$$"
mkdir -p "$tmp_root"

cleanup()
{
    rm -rf "$tmp_root"
    rm -f /tmp/r7-reg-cc-$$ /tmp/r7-reg-ar-$$ /tmp/r7-reg-make-$$
    rm -f /tmp/r7-tar-hook-$$ /tmp/r7-tar-hook-ran-$$
    rm -rf "$repo_root"/build/r7-source-*         "$repo_root"/build/r7-evidence-*         "$repo_root"/build/r7-bundle-stage-*
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

# Canonical-path regression: lexical /usr/bin prefixes must not authorize /tmp.
cat > /tmp/r7-reg-cc-$$ <<EOF
#!/bin/sh
exec "$system_cc" "\$@"
EOF
cat > /tmp/r7-reg-ar-$$ <<EOF
#!/bin/sh
exec "$system_ar" "\$@"
EOF
cat > /tmp/r7-reg-make-$$ <<'EOF'
#!/bin/sh
exec /usr/bin/make "$@"
EOF
chmod +x /tmp/r7-reg-cc-$$ /tmp/r7-reg-ar-$$ /tmp/r7-reg-make-$$

expect_capture_failure canonical-cc     env RUNE_R7_REPEATS=1 CC="/usr/bin/../../tmp/r7-reg-cc-$$" AR="$system_ar"     scripts/r7-run-local.sh
expect_capture_failure canonical-ar     env RUNE_R7_REPEATS=1 CC="$system_cc" AR="/usr/bin/../../tmp/r7-reg-ar-$$"     scripts/r7-run-local.sh
expect_capture_failure canonical-make     env RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar" MAKE="/usr/bin/../../tmp/r7-reg-make-$$"     scripts/r7-run-local.sh

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
cat > /tmp/r7-tar-hook-$$ <<EOF
#!/bin/sh
printf 'ran\n' > /tmp/r7-tar-hook-ran-$$
EOF
chmod +x /tmp/r7-tar-hook-$$
tar_bundle="$tmp_root/tar"
TAR_OPTIONS="--checkpoint=1 --checkpoint-action=exec=/tmp/r7-tar-hook-$$"     RUNE_R7_REPEATS=1 CC="$system_cc" AR="$system_ar"     scripts/r7-run-local.sh "$tar_bundle" >/dev/null
[ ! -e /tmp/r7-tar-hook-ran-$$ ] ||
    fail "TAR_OPTIONS hook executed during capture"
TAR_OPTIONS="--checkpoint=1 --checkpoint-action=exec=/tmp/r7-tar-hook-$$"     /bin/sh "$tar_bundle/command.txt" >/dev/null
[ ! -e /tmp/r7-tar-hook-ran-$$ ] ||
    fail "TAR_OPTIONS hook executed during replay"
if /usr/bin/awk -F '\t' '$16 == 616161 { bad=1 } END { exit bad ? 0 : 1 }' "$tar_bundle/observations.tsv"; then
    fail "tar injection changed the study clock scale"
fi

# Failed destination-filesystem copy must never create the immutable final path.
[ -d /dev/shm ] && [ -w /dev/shm ] ||
    fail "/dev/shm is required for deterministic publication regression"
publish_parent="/dev/shm/r7-publish-regression-$$"
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
