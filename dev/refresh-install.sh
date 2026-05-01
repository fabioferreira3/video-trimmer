#!/usr/bin/env bash
#
# Build the current working tree as `vtrim-local` and install it via pacman,
# replacing any previously-installed `vtrim` / `vtrim-git`.
#
# Driven by the CMake `refresh` custom target:
#     cmake --build build --target refresh
#
# Why this script exists rather than just calling `makepkg -sif --noconfirm`
# directly: makepkg's install step shells out to `pacman -U`, which on a
# conflicting-package detection prompts "Remove vtrim? [y/N]". With
# --noconfirm pacman picks the default (N), the transaction aborts, and the
# whole refresh fails. There is no makepkg/pacman flag to auto-accept that
# specific prompt, so we explicitly remove conflicting packages ourselves
# before calling `pacman -U`. We build first so a compile failure never
# leaves you with no installed `vtrim` at all.

set -euo pipefail

# Resolve repo root from this script's location (dev/refresh-install.sh).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PKGBUILD_DIR="$REPO_ROOT/dist/aur/vtrim-local"

if [[ ! -f "$PKGBUILD_DIR/PKGBUILD" ]]; then
    echo "ERROR: PKGBUILD not found at $PKGBUILD_DIR/PKGBUILD" >&2
    exit 1
fi

# 1. Build the package (no install yet). --force lets us rebuild even if a
# stale .pkg.tar.zst is sitting next to the PKGBUILD.
echo "==> Building vtrim-local from $REPO_ROOT"
(
    cd "$PKGBUILD_DIR"
    makepkg -sf --noconfirm
)

# 2. Locate the freshly-built non-debug package archive. Filename pattern:
#    vtrim-local-<pkgver>-<pkgrel>-<arch>.pkg.tar.zst (the -debug variant has
#    "vtrim-local-debug-" as its prefix, which we want to skip here).
BUILT_PKG=$(
    find "$PKGBUILD_DIR" -maxdepth 1 -type f \
        -name 'vtrim-local-[0-9]*.pkg.tar.zst' \
        -printf '%T@ %p\n' \
        | sort -rn | head -n1 | cut -d' ' -f2-
)
if [[ -z "$BUILT_PKG" ]]; then
    echo "ERROR: built package not found in $PKGBUILD_DIR" >&2
    exit 1
fi
echo "==> Built package: $BUILT_PKG"

# 3. Remove any conflicting upstream packages so the `pacman -U` below does
# not have to ask. We use plain `-R` (not `-Rdd`) so pacman still complains
# if something genuinely depends on vtrim - in practice nothing does.
CONFLICTS=()
for pkg in vtrim vtrim-git; do
    if pacman -Qq "$pkg" >/dev/null 2>&1; then
        CONFLICTS+=("$pkg")
    fi
done
if [[ ${#CONFLICTS[@]} -gt 0 ]]; then
    echo "==> Removing conflicting package(s): ${CONFLICTS[*]}"
    sudo pacman -R --noconfirm "${CONFLICTS[@]}"
fi

# 4. Install (or reinstall, since pkgver() bumps the version every build).
echo "==> Installing $(basename "$BUILT_PKG")"
sudo pacman -U --noconfirm "$BUILT_PKG"

# 5. Friendly summary so the operator can see the new pacman-owned binary.
echo
echo "==> Done. Current /usr/bin/vtrim:"
ls -l /usr/bin/vtrim || true
echo "==> Owned by:"
pacman -Qo /usr/bin/vtrim || true
