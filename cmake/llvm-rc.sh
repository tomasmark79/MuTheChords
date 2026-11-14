#!/usr/bin/env bash
set -euo pipefail
# llvm-rc invokes clang for preprocessing; use the unwrapped cross-capable binary.
export PATH="$(dirname "$(command -v clang-cl)"):$PATH"
exec llvm-rc "$@"
