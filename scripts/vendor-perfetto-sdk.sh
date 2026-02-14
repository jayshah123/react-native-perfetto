#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_DIR="$ROOT_DIR/cpp/third_party/perfetto/sdk"
PERFETTO_VERSION="${PERFETTO_VERSION:-v50.1}"
PERFETTO_REPO_URL="https://github.com/google/perfetto.git"

mkdir -p "$TARGET_DIR"

echo "Vendoring Perfetto SDK ($PERFETTO_VERSION) into $TARGET_DIR"
tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT

git clone --depth 1 --branch "$PERFETTO_VERSION" "$PERFETTO_REPO_URL" "$tmp_dir/perfetto"

cp "$tmp_dir/perfetto/sdk/perfetto.h" "$TARGET_DIR/perfetto.h"
cp "$tmp_dir/perfetto/sdk/perfetto.cc" "$TARGET_DIR/perfetto.cc"

echo "Done. Vendored:"
wc -c "$TARGET_DIR/perfetto.h" "$TARGET_DIR/perfetto.cc"
echo "Rebuild Android/iOS to enable full recording support."
