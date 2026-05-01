# Publishing `video-trimmer` to the AUR

This folder holds the canonical PKGBUILDs for the project. They live in this
repo so upstream changes (new dependencies, changed install layout) and the
matching packaging change always land in the same commit.

The AUR itself is a separate git host — `aur.archlinux.org` — and each AUR
package is its own bare git repo. The flow below is "edit here → copy → push
to AUR".

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

## Publishing a new stable release (`video-trimmer`)

These are the steps to push `v0.1.0` (or any future tag) to the AUR.

### 1. Tag the release in this repo

```bash
cd /home/fabio/projects/video-trimmer
git tag -a v0.1.0 -m "v0.1.0"
git push origin v0.1.0
```

GitHub auto-generates the source tarball at
`https://github.com/fabioferreira3/video-trimmer/archive/refs/tags/v0.1.0.tar.gz`.
This is what the PKGBUILD downloads.

### 2. Compute the real `sha256sum`

The committed PKGBUILD ships with `sha256sums=('SKIP')` because the tarball
hash isn't known until the tag is on GitHub. Compute and paste it in:

```bash
cd /home/fabio/projects/video-trimmer/dist/aur/video-trimmer
curl -fsSL \
  https://github.com/fabioferreira3/video-trimmer/archive/refs/tags/v0.1.0.tar.gz \
  | sha256sum
# → <64 hex chars>  -
```

Edit `PKGBUILD` and replace `SKIP` with that hash.

### 3. Smoke-test the build locally

This actually downloads the tarball, builds it in a clean chroot-like
`makepkg` environment, and produces an installable `.pkg.tar.zst`:

```bash
cd /home/fabio/projects/video-trimmer/dist/aur/video-trimmer
updpkgsums                 # convenience: rewrites sha256sums in place
makepkg --printsrcinfo > .SRCINFO
makepkg -si                # builds, installs, and lets you launch the app
namcap PKGBUILD *.pkg.tar.zst   # lint, address any errors/warnings
```

If `makepkg -si` succeeds and `video-trimmer` launches from your menu / shell,
the package is good.

### 4. Push to the AUR

The AUR repo is a **bare** git repo; the first time you publish, you create
it by simply pushing to a not-yet-existing name (the hook on `aur@` creates
it server-side):

```bash
# clone (or, on first publish, init+remote) the AUR side
cd ~/src                         # or wherever you keep AUR clones
git clone ssh://aur@aur.archlinux.org/video-trimmer.git aur-video-trimmer
cd aur-video-trimmer

# copy the freshly built PKGBUILD and .SRCINFO from this repo
cp /home/fabio/projects/video-trimmer/dist/aur/video-trimmer/PKGBUILD .
cp /home/fabio/projects/video-trimmer/dist/aur/video-trimmer/.SRCINFO .

git add PKGBUILD .SRCINFO
git commit -m "video-trimmer 0.1.0-1: initial release"
git push -u origin master
```

Wait ~30 seconds, then visit `https://aur.archlinux.org/packages/video-trimmer`
to confirm it's live.

## Publishing the `-git` package

This one tracks `master`, so there's no tag step. It only needs to be pushed
once; after that, AUR helpers (`yay`, `paru`) re-clone master on every install
and the `pkgver()` function regenerates the version string.

```bash
git clone ssh://aur@aur.archlinux.org/video-trimmer-git.git aur-video-trimmer-git
cd aur-video-trimmer-git

cp /home/fabio/projects/video-trimmer/dist/aur/video-trimmer-git/PKGBUILD .
cp /home/fabio/projects/video-trimmer/dist/aur/video-trimmer-git/.SRCINFO .

# verify it builds today's master
makepkg -si
namcap PKGBUILD *.pkg.tar.zst

git add PKGBUILD .SRCINFO
git commit -m "video-trimmer-git: initial release"
git push -u origin master
```

## Publishing future updates

Every subsequent release is the same as section "Publishing a new stable
release" but without step 4's `git clone` — just `cd` into your existing
`aur-video-trimmer/` clone, `git pull`, copy the updated files, regenerate
`.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`), commit, and push.

For the `-git` package you only need to push again if **packaging metadata**
changes (new dependency, install layout change). The package itself always
rebuilds against the latest master at install time.

## Common mistakes to avoid

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
