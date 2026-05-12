#!/usr/bin/env bash
#
# Full vtrim release: bump version, commit, tag, push to GitHub, pin the real
# tarball sha256, regenerate .SRCINFO, push the synced packaging metadata back
# to GitHub, and push to the AUR. One command from start to finish.
#
# Usage:
#     dev/release.sh 0.2.0                 # default: full release
#     dev/release.sh 0.2.0 --yes           # skip the confirmation prompt
#     dev/release.sh 0.2.0 --smoke-test    # also `makepkg -si` + `namcap`
#     dev/release.sh 0.2.0 --skip-aur      # everything except the AUR push
#     dev/release.sh 0.2.0 --aur-clone /path/to/aur-vtrim
#
# Environment:
#     VTRIM_AUR_CLONE   default AUR clone path (default: ~/src/aur-vtrim)
#
# Every step is idempotent where it can be (e.g. if the local tag already
# exists, we don't re-create it; if the remote already has it, we abort with
# a clear error). The intent is that re-running the script after a transient
# failure (network blip, AUR SSH timeout) picks up exactly where it left off.

set -euo pipefail

# ---------------------------------------------------------------------------
# 0. Argument parsing.
# ---------------------------------------------------------------------------

NEW_VERSION=""
ASSUME_YES=0
SMOKE_TEST=0
SKIP_AUR=0
AUR_CLONE="${VTRIM_AUR_CLONE:-$HOME/src/aur-vtrim}"

usage() {
    cat <<'EOF'
Usage: dev/release.sh <X.Y.Z> [options]

Options:
    -y, --yes              Skip the confirmation prompt.
    --smoke-test           Also run `makepkg -si` (installs locally) and `namcap`.
    --skip-aur             Do everything except the AUR push.
    --aur-clone PATH       Path to local AUR clone (default: ~/src/aur-vtrim).
    -h, --help             Show this help.

Environment:
    VTRIM_AUR_CLONE        Default value for --aur-clone.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -y|--yes)        ASSUME_YES=1; shift ;;
        --smoke-test)    SMOKE_TEST=1; shift ;;
        --skip-aur)      SKIP_AUR=1; shift ;;
        --aur-clone)     AUR_CLONE="${2:?--aur-clone needs a path}"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        -*)              echo "ERROR: unknown flag: $1" >&2; usage >&2; exit 2 ;;
        *)
            if [[ -n "$NEW_VERSION" ]]; then
                echo "ERROR: too many positional args (got '$NEW_VERSION' and '$1')" >&2
                exit 2
            fi
            NEW_VERSION="$1"; shift ;;
    esac
done

if [[ -z "$NEW_VERSION" ]]; then
    echo "ERROR: missing version argument" >&2
    usage >&2
    exit 2
fi

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: '$NEW_VERSION' is not a valid X.Y.Z version" >&2
    exit 2
fi

# Resolve repo root from this script's location.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AUR_DIR="$REPO_ROOT/dist/aur/vtrim"
TAG="v${NEW_VERSION}"

cd "$REPO_ROOT"

# ---------------------------------------------------------------------------
# 1. Preflight checks.
#
# Bail BEFORE we touch anything if the environment isn't ready. The idea is
# to catch every common foot-gun (wrong branch, dirty tree, missing tools,
# tag already shipped) up front so the operator never gets a half-done
# release in the middle of a longer flow.
# ---------------------------------------------------------------------------

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $1" >&2
        exit 1
    fi
}

require_cmd git
require_cmd makepkg
require_cmd updpkgsums
require_cmd curl
require_cmd awk
require_cmd sed
if [[ "$SMOKE_TEST" -eq 1 ]]; then
    require_cmd namcap
    require_cmd sudo
fi

# Must be on master so the tag we push lands on the right branch.
CUR_BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [[ "$CUR_BRANCH" != "master" ]]; then
    echo "ERROR: must be on 'master', currently on '$CUR_BRANCH'" >&2
    exit 1
fi

# Refuse to run with a dirty tree. We're about to commit + tag + push, and
# accidentally bundling unrelated working-tree changes into a release commit
# is exactly the kind of mistake this script exists to prevent.
if [[ -n "$(git status --porcelain)" ]]; then
    echo "ERROR: working tree is not clean. Commit or stash these first:" >&2
    git status --short >&2
    exit 1
fi

# Refuse if the upstream remote already has the tag — that means the release
# has already been pushed and re-running would either be a no-op or trample
# something.
if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "ERROR: tag '$TAG' already exists locally. Delete it first if you" >&2
    echo "       really want to redo this release:  git tag -d $TAG" >&2
    exit 1
fi
if git ls-remote --tags --exit-code origin "refs/tags/$TAG" >/dev/null 2>&1; then
    echo "ERROR: tag '$TAG' already exists on origin. The release was already shipped." >&2
    exit 1
fi

# Verify the AUR clone is present and looks like a real AUR repo, unless the
# operator explicitly asked us to skip that step.
if [[ "$SKIP_AUR" -ne 1 ]]; then
    if [[ ! -d "$AUR_CLONE/.git" ]]; then
        echo "ERROR: AUR clone not found at $AUR_CLONE" >&2
        echo "       Either clone it once (see dist/aur/README.md) or pass --skip-aur:" >&2
        echo "         git clone ssh://aur@aur.archlinux.org/vtrim.git $AUR_CLONE" >&2
        exit 1
    fi
    AUR_REMOTE=$(git -C "$AUR_CLONE" remote get-url origin 2>/dev/null || true)
    if [[ "$AUR_REMOTE" != *"aur.archlinux.org"* ]]; then
        echo "ERROR: '$AUR_CLONE' does not look like an AUR clone (remote=$AUR_REMOTE)" >&2
        exit 1
    fi
fi

# ---------------------------------------------------------------------------
# 2. Confirmation prompt.
#
# One prompt, summarizing every destructive thing we're about to do. After
# this point the script runs unattended (modulo a sudo password prompt if
# --smoke-test was passed).
# ---------------------------------------------------------------------------

CURRENT_VERSION=$(awk '
    /project\s*\(/ { in_proj = 1 }
    in_proj {
        for (i = 1; i <= NF; i++) {
            if ($i == "VERSION") {
                v = $(i+1); gsub(/[^0-9.]/, "", v); print v; exit
            }
        }
    }
    in_proj && /\)/ { in_proj = 0 }
' "$REPO_ROOT/CMakeLists.txt")

cat <<EOF
=== vtrim release plan ===
  current version  : ${CURRENT_VERSION:-unknown}
  new version      : ${NEW_VERSION}
  git tag          : ${TAG}
  upstream remote  : $(git remote get-url origin)
  AUR clone        : $([[ "$SKIP_AUR" -eq 1 ]] && echo "(skipped)" || echo "$AUR_CLONE")
  smoke-test       : $([[ "$SMOKE_TEST" -eq 1 ]] && echo "yes (makepkg -si + namcap)" || echo "no")

This will:
  1. Bump CMakeLists.txt + dist/aur/vtrim/PKGBUILD to ${NEW_VERSION}.
  2. Commit, tag ${TAG}, and push to origin/master.
  3. Run updpkgsums to pin the real sha256 from GitHub.
  4. Regenerate dist/aur/vtrim/.SRCINFO.
$([[ "$SMOKE_TEST" -eq 1 ]] && echo "  5. makepkg -si (installs locally) + namcap.")
  $([[ "$SMOKE_TEST" -eq 1 ]] && echo "6" || echo "5"). Commit the synced PKGBUILD + .SRCINFO and push to origin/master.
$([[ "$SKIP_AUR" -eq 1 ]] || echo "  $([[ "$SMOKE_TEST" -eq 1 ]] && echo "7" || echo "6"). Push PKGBUILD + .SRCINFO to the AUR (${AUR_CLONE}).")

EOF

if [[ "$ASSUME_YES" -ne 1 ]]; then
    read -r -p "Proceed? [y/N] " reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) echo "Aborted."; exit 1 ;;
    esac
fi

step() {
    echo
    echo "==> $*"
}

# ---------------------------------------------------------------------------
# 3. Bump version locally (delegates to bump-version.sh).
# ---------------------------------------------------------------------------

step "Bumping version to ${NEW_VERSION}"
"$SCRIPT_DIR/bump-version.sh" "$NEW_VERSION" >/dev/null

# ---------------------------------------------------------------------------
# 4. Commit + tag + push to GitHub.
#
# We stage only the two files bump-version.sh edits, never `git add -A`, so
# any stray untracked files in the working tree (build artifacts, leftover
# makepkg `pkg/`/`src/` from a previous run) stay out of the release commit.
# ---------------------------------------------------------------------------

step "Committing bump + tagging ${TAG}"
git add CMakeLists.txt dist/aur/vtrim/PKGBUILD
if git diff --cached --quiet; then
    echo "    nothing to commit (working tree already at ${NEW_VERSION})"
else
    git commit -m "Bump to ${NEW_VERSION}"
fi
git tag -a "$TAG" -m "$TAG"

step "Pushing master + ${TAG} to origin"
git push origin master "$TAG"

# ---------------------------------------------------------------------------
# 5. Pin the real sha256 from the freshly-published GitHub tarball.
#
# GitHub generates archive tarballs on demand the moment a tag exists, so
# this should succeed on the first try - but allow a couple of retries to
# absorb transient network or CDN propagation hiccups.
# ---------------------------------------------------------------------------

TARBALL_URL="$(git remote get-url origin)"
TARBALL_URL="${TARBALL_URL%.git}/archive/refs/tags/${TAG}.tar.gz"

step "Verifying tarball is live at ${TARBALL_URL}"
for attempt in 1 2 3 4 5; do
    if curl -sfI -L "$TARBALL_URL" >/dev/null; then
        echo "    tarball is available"
        break
    fi
    if [[ "$attempt" -eq 5 ]]; then
        echo "ERROR: tarball never became available at $TARBALL_URL" >&2
        echo "       Re-run with the same version once GitHub is reachable." >&2
        exit 1
    fi
    echo "    attempt $attempt failed, retrying in 3s..."
    sleep 3
done

step "Pinning sha256 via updpkgsums"
(cd "$AUR_DIR" && updpkgsums)

step "Regenerating .SRCINFO"
(cd "$AUR_DIR" && makepkg --printsrcinfo > .SRCINFO)

# ---------------------------------------------------------------------------
# 6. Optional smoke test: actually build + install + lint.
# ---------------------------------------------------------------------------

if [[ "$SMOKE_TEST" -eq 1 ]]; then
    step "Smoke-test: makepkg -si (will prompt for sudo password)"
    (cd "$AUR_DIR" && makepkg -si --noconfirm)

    step "Smoke-test: namcap"
    (cd "$AUR_DIR" && namcap PKGBUILD ./*.pkg.tar.zst)
fi

# ---------------------------------------------------------------------------
# 7. Commit the synced packaging metadata back to upstream.
# ---------------------------------------------------------------------------

step "Committing synced PKGBUILD + .SRCINFO to upstream"
git add dist/aur/vtrim/PKGBUILD dist/aur/vtrim/.SRCINFO
if git diff --cached --quiet; then
    echo "    nothing to commit (already in sync)"
else
    git commit -m "vtrim ${NEW_VERSION}-1: sync PKGBUILD + .SRCINFO"
    git push origin master
fi

# ---------------------------------------------------------------------------
# 8. Push to the AUR.
#
# The AUR clone is a bare-style repo containing only PKGBUILD + .SRCINFO
# (plus an optional .gitignore). We explicitly `git add` only those two
# files so any `pkg/`/`src/`/`*.pkg.tar.*` leftovers from local builds in
# that clone never end up on the AUR.
# ---------------------------------------------------------------------------

if [[ "$SKIP_AUR" -ne 1 ]]; then
    step "Syncing PKGBUILD + .SRCINFO into $AUR_CLONE"
    (
        cd "$AUR_CLONE"
        git pull --ff-only
        cp "$AUR_DIR/PKGBUILD" .
        cp "$AUR_DIR/.SRCINFO" .
        git add PKGBUILD .SRCINFO
        if git diff --cached --quiet; then
            echo "    AUR clone already up to date"
        else
            git commit -m "vtrim ${NEW_VERSION}-1: upstream release"
            git push
        fi
    )
fi

# ---------------------------------------------------------------------------
# 9. Done.
# ---------------------------------------------------------------------------

cat <<EOF

=== Release ${NEW_VERSION} complete ===
  GitHub tag       : $(git remote get-url origin | sed 's/\.git$//')/releases/tag/${TAG}
$([[ "$SKIP_AUR" -eq 1 ]] || echo "  AUR page         : https://aur.archlinux.org/packages/vtrim")

EOF
