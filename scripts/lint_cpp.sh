#!/usr/bin/env bash
# Runs cpplint (Google C++ style guide checker) over src/ and include/.
# Settings live in CPPLINT.cfg at the repo root.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v cpplint >/dev/null 2>&1; then
    echo "cpplint not found. Install it with: pip install --user cpplint" >&2
    exit 1
fi

cpplint --recursive src include
