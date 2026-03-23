#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMMA_SYSROOT="${COMMA_SYSROOT:-$HOME/comma-sysroot}"
RUNTIME_LINK_DIR="$REPO_ROOT/third_party/runtime_link_libs"
WRAPPER_DIR="$REPO_ROOT/.codex_tmp/bin"

if [[ ! -d "$COMMA_SYSROOT" ]]; then
  echo "Missing COMMA_SYSROOT: $COMMA_SYSROOT" >&2
  exit 1
fi

mkdir -p "$RUNTIME_LINK_DIR" "$WRAPPER_DIR"
rm -f "$RUNTIME_LINK_DIR"/*

# Force the linker to prefer comma-compatible runtime libs from the sysroot.
declare -A link_map=(
  [libcapnp.so]="$COMMA_SYSROOT/usr/local/lib/libcapnp.so"
  [libcapnp-1.0.2.so]="$COMMA_SYSROOT/usr/local/lib/libcapnp-1.0.2.so"
  [libkj.so]="$COMMA_SYSROOT/usr/local/lib/libkj.so"
  [libkj-1.0.2.so]="$COMMA_SYSROOT/usr/local/lib/libkj-1.0.2.so"
  [libavcodec.so]="$COMMA_SYSROOT/usr/local/lib/libavcodec.so"
  [libavcodec.so.58]="$COMMA_SYSROOT/usr/local/lib/libavcodec.so.58"
  [libavformat.so]="$COMMA_SYSROOT/usr/local/lib/libavformat.so"
  [libavformat.so.58]="$COMMA_SYSROOT/usr/local/lib/libavformat.so.58"
  [libavutil.so]="$COMMA_SYSROOT/usr/local/lib/libavutil.so"
  [libavutil.so.56]="$COMMA_SYSROOT/usr/local/lib/libavutil.so.56"
  [libOmxCore.so]="$COMMA_SYSROOT/lib/aarch64-linux-gnu/libOmxCore.so"
)

for name in "${!link_map[@]}"; do
  target="${link_map[$name]}"
  if [[ ! -e "$target" ]]; then
    echo "Missing sysroot runtime lib: $target" >&2
    exit 1
  fi
  ln -sf "$target" "$RUNTIME_LINK_DIR/$name"
done

cat > "$WRAPPER_DIR/cythonize" <<'EOF'
#!/usr/bin/env bash
exec python3 -m Cython.Build.Cythonize "$@"
EOF
chmod +x "$WRAPPER_DIR/cythonize"

cleanup() {
  sudo rm -f /TICI
}
trap cleanup EXIT

sudo touch /TICI

cd "$REPO_ROOT"
rm -f selfdrive/ui/ui common/params_pyx.so

PATH="$WRAPPER_DIR:$PATH" \
COMMA_SYSROOT="$COMMA_SYSROOT" \
/usr/bin/scons -j"$(nproc)" selfdrive/ui/ui common/params_pyx.so

echo
echo "Built device-ABI artifacts:"
objdump -p selfdrive/ui/ui | grep NEEDED | sed -n '1,40p'
file common/params_pyx.so
