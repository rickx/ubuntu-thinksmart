#!/bin/bash
# Compatibility wrapper for the repo-root build workflow.

set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "$SCRIPT_DIR/build-wsl.sh"
