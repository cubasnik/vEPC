#!/usr/bin/env bash
set -euo pipefail

# Safe deploy helper:
# - fetches remote
# - refuses to deploy if there are uncommitted local changes
# - pulls remote fast-forward if possible
# - runs docker compose up --build for specified services or all by default

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_DIR"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "On branch $BRANCH"

git fetch origin

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "There are uncommitted changes. Commit or stash them before deploy."
  git status --porcelain
  exit 1
fi

UPSTREAM="origin/$BRANCH"
if ! git rev-parse --verify "$UPSTREAM" >/dev/null 2>&1; then
  echo "Upstream $UPSTREAM not found. Skipping pull."
else
  LOCAL=$(git rev-parse @)
  REMOTE=$(git rev-parse @{u})
  BASE=$(git merge-base @ @{u})
  if [ "$LOCAL" = "$REMOTE" ]; then
    echo "Local is up-to-date with remote."
  elif [ "$LOCAL" = "$BASE" ]; then
    echo "Remote ahead, pulling..."
    git pull --ff-only
  elif [ "$REMOTE" = "$BASE" ]; then
    echo "Local ahead of remote, will push after deploy if desired."
  else
    echo "Branches have diverged. Manual intervention required."
    exit 2
  fi
fi

# Run compose (allow optional service names)
SERVICES=("$@")
if [ ${#SERVICES[@]} -gt 0 ]; then
  docker compose -f docker-compose.yml up -d --build "${SERVICES[@]}"
else
  docker compose -f docker-compose.yml up -d --build
fi

echo "Deploy finished."
