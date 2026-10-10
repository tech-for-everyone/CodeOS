#!/bin/bash
# Auto-push script for CodeOS project
# Pushes main branch and Zircon submodule to GitHub

set -e

CODEOS_DIR="/home/codeosuser/CodeOS"
ZIRCON_DIR="/home/codeosuser/CodeOS/Zircon"

echo "=== CodeOS Auto-Push Script ==="
echo "CodeOS dir: $CODEOS_DIR"
echo "Zircon dir: $ZIRCON_DIR"
echo ""

# Function to push a git repo
push_repo() {
    local dir=$1
    local remote=$2
    local branch=$3
    
    echo ">>> Pushing $dir to $remote/$branch"
    cd "$dir"
    
    # Check for uncommitted changes
    if ! git diff --quiet || ! git diff --cached --quiet; then
        echo "  Uncommitted changes detected, committing..."
        git add -A
        git commit -m "auto-push: $(date '+%Y-%m-%d %H:%M:%S')"
    fi
    
    # Push
    git push "$remote" "$branch"
    echo "  ✓ Pushed successfully"
}

# 1. Push CodeOS main
echo "1. Pushing CodeOS main branch..."
push_repo "$CODEOS_DIR" codeos main

# 2. Push Zircon
echo ""
echo "2. Pushing Zircon repo..."
push_repo "$ZIRCON_DIR" origin main

# 3. Optionally push kernel-shell branch if it has changes
echo ""
echo "3. Checking kernel-shell branch..."
cd "$CODEOS_DIR"
if git rev-parse --verify codeos/kernel-shell >/dev/null 2>&1; then
    echo "  kernel-shell branch exists on remote"
    # Uncomment below to push kernel-shell if needed
    # push_repo "$CODEOS_DIR" codeos kernel-shell
else
    echo "  kernel-shell not found on remote"
fi

echo ""
echo "=== All pushes completed ==="