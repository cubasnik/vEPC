#!/usr/bin/env bash
set -euo pipefail

# Auto commit & push script
# Usage: ./tools/auto_push.sh [commit-prefix]
# Example: ./tools/auto_push.sh auto

PREFIX=${1:-auto}
REPO_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
cd "$REPO_ROOT"

timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
msg="$PREFIX: $timestamp"

# Stage all changes
git add -A

# Exit if nothing to commit
if git diff --staged --quiet && git diff --quiet; then
  echo "No changes to commit"
  exit 0
fi

# Commit and push current branch
branch=$(git rev-parse --abbrev-ref HEAD)
git commit -m "$msg"
git push -u origin "$branch"

echo "Pushed commit on $branch: $msg"
