#!/usr/bin/env bash
#
# Bump the project version locally - just the source-file edits, no git, no
# tagging, no AUR push.
#
# For the one-command "ship a release" flow (bump + commit + tag + push +
# updpkgsums + .SRCINFO + push to AUR), use dev/release.sh instead. This
# script is for when you want to bump but not release - e.g. to test the
# new pkgver locally via vtrim-local first.
#
# Usage:
#     dev/bump-version.sh 0.2.0
#
# Why a script at all: the project version lives in several places by
# necessity, but only ONE of them is a true source: CMakeLists.txt's
# `project(... VERSION ...)`. Everything else either reads from it at build
# time (vtrim-local PKGBUILD, the compiled-in VTRIM_VERSION used by
# `vtrim --version`) or has to embed a literal pkgver because the AUR clone
# has no access to the source tree at parse time (dist/aur/vtrim/PKGBUILD).
# This script keeps that one stubborn literal in sync with CMakeLists.txt
# and resets the packaging metadata.
#
# What this script does NOT do (on purpose):
#   - Edit src/main.cpp: VTRIM_VERSION is a CMake compile definition, so the
#     binary already prints whatever PROJECT_VERSION is at build time.
#   - Edit dist/aur/vtrim-git/PKGBUILD: its pkgver() function derives the
#     version from `git describe` at build time.
#   - Edit dist/aur/vtrim-local/PKGBUILD: its pkgver() function parses
#     CMakeLists.txt at build time.
#   - Create git commits/tags, touch GitHub, or touch the AUR. See
#     dev/release.sh for the end-to-end flow.

set -euo pipefail

# ---------------------------------------------------------------------------
# 0. Argument parsing.
# ---------------------------------------------------------------------------

if [[ $# -ne 1 ]]; then
    cat >&2 <<'EOF'
Usage: dev/bump-version.sh <NEW_VERSION>

NEW_VERSION must be a semver-ish X.Y.Z triple (e.g. 0.2.0).

Examples:
    dev/bump-version.sh 0.2.0
    dev/bump-version.sh 1.0.0
EOF
    exit 2
fi

NEW_VERSION="$1"

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: '$NEW_VERSION' is not a valid X.Y.Z version" >&2
    exit 2
fi

# Resolve repo root from this script's location (dev/bump-version.sh).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CMAKE_FILE="$REPO_ROOT/CMakeLists.txt"
AUR_PKGBUILD="$REPO_ROOT/dist/aur/vtrim/PKGBUILD"

for f in "$CMAKE_FILE" "$AUR_PKGBUILD"; do
    if [[ ! -f "$f" ]]; then
        echo "ERROR: expected file not found: $f" >&2
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# 1. Read current version from CMakeLists.txt.
#
# Use awk to find the VERSION token inside the project(...) block so we don't
# accidentally match a stray "VERSION" elsewhere in the file (e.g. in a
# comment or a find_package call). Stop at the first match.
# ---------------------------------------------------------------------------

CURRENT_VERSION=$(awk '
    /project\s*\(/ { in_proj = 1 }
    in_proj {
        for (i = 1; i <= NF; i++) {
            if ($i == "VERSION") {
                v = $(i+1)
                gsub(/[^0-9.]/, "", v)
                print v
                exit
            }
        }
    }
    in_proj && /\)/ { in_proj = 0 }
' "$CMAKE_FILE")

if [[ -z "$CURRENT_VERSION" ]]; then
    echo "ERROR: could not parse current VERSION from $CMAKE_FILE" >&2
    exit 1
fi

if [[ "$CURRENT_VERSION" == "$NEW_VERSION" ]]; then
    echo "Version is already $NEW_VERSION; nothing to do." >&2
    exit 0
fi

echo "==> Bumping vtrim: $CURRENT_VERSION -> $NEW_VERSION"

# ---------------------------------------------------------------------------
# 2. Update CMakeLists.txt - the single source of truth.
#
# The pattern matches a VERSION token followed by whitespace and an X.Y.Z
# triple, so it only touches the project() declaration.
# ---------------------------------------------------------------------------

sed -i -E "s/(VERSION[[:space:]]+)[0-9]+\.[0-9]+\.[0-9]+/\1${NEW_VERSION}/" "$CMAKE_FILE"
echo "    [updated] $CMAKE_FILE (project VERSION)"

# ---------------------------------------------------------------------------
# 3. Sync the stable AUR PKGBUILD.
#
#   - pkgver: set to the new version literally.
#   - pkgrel: reset to 1 (only bump pkgrel for packaging-only fixes against
#     an unchanged upstream version).
#   - sha256sums: reset to SKIP until the GitHub tag exists; the operator
#     runs `updpkgsums` later to fill in the real hash.
# ---------------------------------------------------------------------------

sed -i -E "s/^pkgver=.*/pkgver=${NEW_VERSION}/" "$AUR_PKGBUILD"
sed -i -E "s/^pkgrel=.*/pkgrel=1/" "$AUR_PKGBUILD"
sed -i -E "s/^sha256sums=.*/sha256sums=('SKIP')/" "$AUR_PKGBUILD"
echo "    [updated] $AUR_PKGBUILD (pkgver, pkgrel=1, sha256sums=SKIP)"

# ---------------------------------------------------------------------------
# 4. Friendly summary + next-step cheat sheet.
# ---------------------------------------------------------------------------

cat <<EOF

Bumped to ${NEW_VERSION} (local files only). Nothing has been committed,
tagged, or pushed.

For the full release flow (commit + tag + push + sha256 + .SRCINFO + AUR),
run this script's bigger sibling instead next time:

    dev/release.sh ${NEW_VERSION}

EOF
