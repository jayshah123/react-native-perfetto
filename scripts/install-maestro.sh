#!/usr/bin/env bash
set -euo pipefail

mkdir -p "$HOME/.maestro"

if command -v maestro >/dev/null 2>&1; then
  echo "Maestro CLI is already installed: $(command -v maestro)"
  maestro --version
  exit 0
fi

echo "Installing Maestro CLI..."
curl -fsSL "https://get.maestro.mobile.dev" | bash

if command -v maestro >/dev/null 2>&1; then
  echo "Maestro CLI installed: $(command -v maestro)"
  maestro --version
  exit 0
fi

if [ -x "$HOME/.maestro/bin/maestro" ]; then
  echo "Maestro CLI installed at $HOME/.maestro/bin/maestro"
  "$HOME/.maestro/bin/maestro" --version
  echo "Add $HOME/.maestro/bin to your PATH to use 'maestro' directly."
  exit 0
fi

echo "Failed to find Maestro CLI after installation." >&2
exit 1
