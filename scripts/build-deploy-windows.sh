#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

./scripts/build-plugin.sh windows
python3 scripts/deploy-windows.py
