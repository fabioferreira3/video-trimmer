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
*"Qt Video Trimmer"*, distinct from GNOME's "Video Trimmer".)

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

Every subsequent release is the same as section "Publishing the first stable
release" but without step 4's `git clone` — just `cd` into your existing
`aur-vtrim/` clone, `git pull`, copy the updated files, regenerate
`.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`), commit, and push.

For the `-git` package you only need to push again if **packaging metadata**
changes (new dependency, install layout change). The package itself always
rebuilds against the latest master at install time.

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
