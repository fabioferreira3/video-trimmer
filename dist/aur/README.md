# Publishing `vtrim` to the AUR

This folder holds the canonical PKGBUILDs for the project. They live in this
repo so upstream changes (new dependencies, changed install layout) and the
matching packaging change always land in the same commit.

The AUR itself is a separate git host — `aur.archlinux.org` — and each AUR
package is its own bare git repo. The flow below is "edit here → copy → push
to AUR".

> **Why is the package called `vtrim` and not `video-trimmer`?**
> Because `extra/video-trimmer` (the GTK4 GNOME Circle app at
> https://apps.gnome.org/VideoTrimmer/) already owns that name and the
> `/usr/bin/video-trimmer` path. Submitting an AUR package called
> `video-trimmer` is hard-rejected by the AUR's hook
> (`error: package already provided by [extra]: video-trimmer`), and even if
> it weren't, two packages installing the same `/usr/bin` file would refuse
> to coexist. `vtrim` is short, distinct, and lets users have both apps
> installed at once. The launcher entry still reads "Qt Video Trimmer".

## One-time setup

1. **Move your AUR SSH key out of this repo.** It must not be committed.

   ```bash
   mkdir -p ~/.ssh
   mv /home/fabio/projects/video-trimmer/aur     ~/.ssh/aur
   mv /home/fabio/projects/video-trimmer/aur.pub ~/.ssh/aur.pub
   chmod 600 ~/.ssh/aur
   chmod 644 ~/.ssh/aur.pub
   ```

2. **Tell SSH to use that key for `aur.archlinux.org`.** Append to
   `~/.ssh/config`:

   ```sshconfig
   Host aur.archlinux.org
       User aur
       IdentityFile ~/.ssh/aur
       IdentitiesOnly yes
   ```

3. **Smoke-test the SSH auth.** You should see your AUR username:

   ```bash
   ssh aur@aur.archlinux.org help
   # → Welcome to AUR, <yourname>!  ...
   ```

4. **Install the packaging toolchain** (only needed locally, not on consumers'
   machines):

   ```bash
   sudo pacman -S --needed base-devel namcap pacman-contrib
   ```

## Publishing the first stable release (`vtrim 0.1.1-1`)

> **Why 0.1.1 and not 0.1.0?** The original `v0.1.0` tag was pushed to
> GitHub before we discovered the `video-trimmer` name collision and ended
> up renaming the binary + AUR package. Bumping to `0.1.1` is the
> least-destructive way to ship the renamed build without rewriting git
> tag history. `v0.1.0` stays on GitHub as a historical artifact (it
> builds the old `video-trimmer` binary that nobody could install
> anyway).

### 1. Tag the release in this repo

```bash
cd /home/fabio/projects/video-trimmer
git add -A
git commit -m "Rename binary and AUR package to vtrim, bump to 0.1.1"
git tag -a v0.1.1 -m "v0.1.1: rename to vtrim"
git push origin master v0.1.1
```

GitHub auto-generates the source tarball at
`https://github.com/fabioferreira3/video-trimmer/archive/refs/tags/v0.1.1.tar.gz`.
This is what the PKGBUILD downloads. Note the URL still contains
`video-trimmer` because the **GitHub repo** keeps that name; only the
binary and AUR package are renamed.

### 2. Compute the real `sha256sum`

The committed PKGBUILD ships with `sha256sums=('SKIP')` because the tarball
hash isn't known until the tag is on GitHub. Compute and paste it in:

```bash
cd /home/fabio/projects/video-trimmer/dist/aur/vtrim
curl -fsSL \
  https://github.com/fabioferreira3/video-trimmer/archive/refs/tags/v0.1.1.tar.gz \
  | sha256sum
# → <64 hex chars>  -
```

Edit `PKGBUILD` and replace `SKIP` with that hash.

### 3. Smoke-test the build locally

This actually downloads the tarball, builds it in a clean `makepkg`
environment, and produces an installable `.pkg.tar.zst`:

```bash
cd /home/fabio/projects/video-trimmer/dist/aur/vtrim
updpkgsums                 # convenience: rewrites sha256sums in place
makepkg --printsrcinfo > .SRCINFO
makepkg -si                # builds, installs, and lets you launch the app
namcap PKGBUILD *.pkg.tar.zst   # lint, address any errors/warnings
```

If `makepkg -si` succeeds and `vtrim` launches from your menu / shell, the
package is good. (You should see the app's launcher entry as
*"Video Trimmer"*, distinct from GNOME's "Video Trimmer".)

### 4. Push to the AUR

The AUR repo is a **bare** git repo; the first time you publish, you create
it by simply pushing to a not-yet-existing name (the hook on `aur@` creates
it server-side):

> **Heads up:** If you still have the old `aur-video-trimmer/` clone from
> the failed first push, **delete it** — it points at the wrong remote.
>
> ```bash
> rm -rf ~/src/aur-video-trimmer   # or wherever you put it
> ```

```bash
# clone (or, on first publish, init+remote) the AUR side
cd ~/src                         # or wherever you keep AUR clones
git clone ssh://aur@aur.archlinux.org/vtrim.git aur-vtrim
cd aur-vtrim

# copy the freshly built PKGBUILD and .SRCINFO from this repo
cp /home/fabio/projects/video-trimmer/dist/aur/vtrim/PKGBUILD .
cp /home/fabio/projects/video-trimmer/dist/aur/vtrim/.SRCINFO .

git add PKGBUILD .SRCINFO
git commit -m "vtrim 0.1.1-1: initial release"
git push -u origin master
```

Wait ~30 seconds, then visit `https://aur.archlinux.org/packages/vtrim` to
confirm it's live.

## Publishing the `vtrim-git` package

This one tracks `master`, so there's no tag step. It only needs to be pushed
once; after that, AUR helpers (`yay`, `paru`) re-clone master on every install
and the `pkgver()` function regenerates the version string.

```bash
git clone ssh://aur@aur.archlinux.org/vtrim-git.git aur-vtrim-git
cd aur-vtrim-git

cp /home/fabio/projects/video-trimmer/dist/aur/vtrim-git/PKGBUILD .
cp /home/fabio/projects/video-trimmer/dist/aur/vtrim-git/.SRCINFO .

# verify it builds today's master
makepkg -si
namcap PKGBUILD *.pkg.tar.zst

git add PKGBUILD .SRCINFO
git commit -m "vtrim-git: initial release"
git push -u origin master
```

## Publishing future updates

The version lives in exactly one place in source: the
`project(vtrim VERSION X.Y.Z)` declaration in the top-level `CMakeLists.txt`.
Every other place that has to know the version derives from it:

- `src/main.cpp` gets it via a `VTRIM_VERSION` compile definition baked in
  from `${PROJECT_VERSION}` (see `target_compile_definitions` in
  `CMakeLists.txt`), so `vtrim --version` always matches the build.
- `dist/aur/vtrim-local/PKGBUILD` parses `CMakeLists.txt` in its `pkgver()`.
- `dist/aur/vtrim-git/PKGBUILD` derives from `git describe --long --tags`.

The stable `dist/aur/vtrim/PKGBUILD` is the only file that has to embed a
literal `pkgver=X.Y.Z` — its build runs inside a fresh AUR clone with no
source tree available at parse time, and the GitHub tarball URL has to be
constructed from `$pkgver` before any source is fetched. The release script
keeps that literal in sync.

### One-command release: `dev/release.sh`

From the repo root, with a clean working tree on `master`:

```bash
./dev/release.sh 0.2.0
```

That single command:

1. Preflight-checks: right branch, clean tree, tag not already shipped,
   AUR clone present, required tools (`makepkg`, `updpkgsums`, `curl`, …).
2. Bumps `CMakeLists.txt` + `dist/aur/vtrim/PKGBUILD` to the new version,
   resets `pkgrel=1`, resets `sha256sums=('SKIP')`.
3. Commits the bump, creates the `vX.Y.Z` annotated tag, pushes both to
   GitHub.
4. Waits for the GitHub tarball to be live, then runs `updpkgsums` to pin
   the real sha256 and `makepkg --printsrcinfo > .SRCINFO`.
5. Commits the synced `PKGBUILD` + `.SRCINFO` back to GitHub.
6. `cd`s into your AUR clone, copies the two files in, commits, and pushes
   to `aur.archlinux.org/vtrim`.

After it prints `Release X.Y.Z complete`, the AUR page should show the new
version within ~30 seconds.

Useful flags:

| Flag | Purpose |
|------|---------|
| `-y`, `--yes` | Skip the confirmation prompt. |
| `--smoke-test` | Additionally run `makepkg -si` (installs locally) and `namcap`. |
| `--skip-aur` | Do everything except the AUR push (useful if AUR SSH is broken). |
| `--aur-clone PATH` | Override the AUR clone location (default `~/src/aur-vtrim`, or `$VTRIM_AUR_CLONE`). |

Every step is idempotent where possible: if a transient failure stops the
script halfway (e.g. AUR push times out), you can re-run it with the same
version. It refuses to re-tag if the tag is already on the remote, and
skips git commits that have nothing new to commit.

### Lightweight bump (no release)

If you want to bump `CMakeLists.txt` + the stable `PKGBUILD` without
committing or pushing anything (e.g. to verify a `vtrim-local` build picks
up the new version first), there's a smaller sibling:

```bash
./dev/bump-version.sh 0.2.0
```

It does only steps 1–2 from the release flow above, then leaves the working
tree edited so you can inspect, test, and commit by hand.

### Notes on the other two packages

- **`vtrim-git`**: only push to the AUR again if **packaging metadata**
  changes (new dependency, install layout change). The package itself always
  rebuilds against the latest master at install time, and its `pkgver()`
  regenerates from `git describe` on each install. `release.sh` does
  **not** touch this AUR repo.
- **`vtrim-local`**: never goes to the AUR. Its `pkgver()` reads
  `CMakeLists.txt`, so `cmake --build build --target refresh` automatically
  produces packages named `vtrim-local-<new-version>.local.<timestamp>`
  after a bump — no further action required.

## Common mistakes to avoid

- **Don't pick an AUR pkgname that already exists in `[core]`/`[extra]`.**
  The submission hook hard-rejects with
  `error: package already provided by [extra]: <name>`. Check first with
  `pacman -Si <name>` *and* `pacman -Ss <name>` (the second catches packages
  that `provides=<name>` under a different `pkgname`). The AUR's RPC search
  alone is not enough — it only sees the AUR, not the official repos.

- **Never commit a built `.pkg.tar.zst`, `pkg/`, or `src/` directory to the
  AUR repo.** AUR repos must contain only `PKGBUILD`, `.SRCINFO`, and any
  small auxiliary files (e.g. `.install` scripts, patches). Add a
  `.gitignore` to your AUR clone if you build inside it:

  ```
  pkg/
  src/
  *.pkg.tar.*
  *.tar.gz
  ```

- **`.SRCINFO` must always match `PKGBUILD`.** The AUR web UI reads
  `.SRCINFO` for search/dep display; `makepkg` reads `PKGBUILD` for the
  actual build. Run `makepkg --printsrcinfo > .SRCINFO` before every commit.

- **Don't bump `pkgrel` for upstream changes**, only for packaging-only
  changes (e.g. fixing a missing dep). New upstream version → bump `pkgver`,
  reset `pkgrel=1`.

- **Don't run `makepkg` as root.** It refuses anyway.
