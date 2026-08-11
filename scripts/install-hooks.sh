#!/bin/sh
# Point git at the tracked hooks in .githooks/.
#
# Hooks can't be committed into .git/hooks, so this is the one manual
# step. core.hooksPath is per-clone, so every clone needs it once.
set -e

cd "$(dirname "$0")/.."
git config core.hooksPath .githooks
chmod +x .githooks/*

echo "Hooks installed (core.hooksPath -> .githooks)."

if ! command -v gitleaks >/dev/null 2>&1; then
    echo
    echo "WARNING: gitleaks is not installed, so the hook will no-op."
    echo "         brew install gitleaks"
fi
