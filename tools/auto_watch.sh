#!/usr/bin/env bash
set -euo pipefail

# Watch repository for changes and call auto_push.sh
# Requires: fswatch (brew install fswatch)
# Usage: ./tools/auto_watch.sh [commit-prefix]

PREFIX=${1:-auto}

if ! command -v fswatch >/dev/null 2>&1; then
  echo "fswatch not found. Install: brew install fswatch"
  exit 2
fi

REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
cd "$REPO_ROOT"

echo "Watching $REPO_ROOT for changes (excludes .git and build)..."

# fswatch -0 produces NUL-separated events
fswatch -0 -r --exclude="^\.git($|/)" --exclude="^build($|/)" . | while read -d "" event; do
  echo "Change detected: $event"
  ./tools/auto_push.sh "$PREFIX" || echo "auto_push failed"
done
