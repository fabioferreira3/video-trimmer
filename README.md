# Video Trimmer

A simple, millisecond-precision desktop video and audio trimmer for Linux,
written in C++ with Qt 6 and FFmpeg. Designed to be the smallest sensible app
that lets you open a media file, mark exact In/Out points, preview the
selection, and export a trimmed copy.

Primary target is Arch Linux. It should also work on any modern Linux distro
that ships Qt 6.5+ and FFmpeg.

## Features

- Embedded media preview powered by `QMediaPlayer` (Qt's FFmpeg backend).
  Audio-only files (`.mp3`, `.m4a`, `.flac`, `.wav`, `.ogg`, `.opus`, …) get
  a dedicated speaker placeholder in place of the black video pane.
- Custom timeline widget with a draggable playhead and **In / Out handles**
  that show a shaded selection — same controls for video and audio.
- **Millisecond-precision** time handling everywhere; current/total time is
  always displayed as `HH:MM:SS.mmm`.
- **Editable In/Out time fields** in the transport bar: type a precise
  timestamp (`00:12:25`, `12:25.500`, `90`, etc.) and press Enter to jump
  the marker exactly there — no scrubbing required.
- Keyboard nudging at 1 / 10 / 100 / 1000 ms granularity for both the
  playhead and the In/Out points.
- "Play within selection" semantics: pressing Play (button, Space, or `L`)
  always starts at the In point and stops at the Out point.
- Toggleable **Loop** for the trim selection — when on, playback restarts
  from In whenever it reaches Out.
- **Crop to Selection** (`Ctrl+K`, or the *Crop* button in the transport
  bar) — reduces the working clip to the current `[In, Out]` and reloads
  the player with that fragment so you can keep refining the trim from
  there. Uses stream copy under the hood, so it's near-instant and
  lossless even across multiple iterations. The original file on disk is
  never modified; the cropped clip lives in a temp file that's cleaned up
  when you close the file or quit the app.
- **Cut Selection** (`Ctrl+Shift+K`, or the *Cut* button in the transport
  bar) — the inverse of Crop: removes the `[In, Out]` segment from the
  working clip and stitches the surviving `[0, In]` and `[Out, end]`
  parts back together. Repeatable: chain as many cuts as you need to
  remove multiple unwanted regions. Also stream-copy under the hood, so
  iterating doesn't degrade quality. Original file is untouched; the
  edited working clip lives in a temp file with the same lifecycle as a
  cropped one.
- Two export modes (auto-adapted for video vs. audio sources):
  - **Precise** (default) — re-encodes for millisecond-accurate boundaries.
    Video sources go to MP4 (H.264 + AAC); audio-only sources let you pick
    the output **format** (M4A/AAC, MP3, Opus, OGG/Vorbis, FLAC, or WAV).
  - **Fast** — `ffmpeg -c copy`, near-instant but snaps to the nearest
    keyframe of the source. Container is preserved.
- For audio-only sources the export dialog shows an **Audio Format** picker
  (defaulting to whatever matches the source codec, e.g. `.mp3` in →
  `.mp3` out) and hides the **Frame Rate** group, which has no meaning for
  audio.
- Modal export progress dialog with cancel support, parsed from
  `ffmpeg -progress pipe:1`.
- **Audio output device picker** under `Audio → Output Device`. Pin a specific
  device (USB headset, HDMI, etc.) so playback always routes to it,
  surviving pause/play and PipeWire stream churn.
- Drag-and-drop file open, recent files menu, window-state persistence via
  `QSettings`.

## Requirements

| | Minimum |
|---|---|
| OS | Linux (tested on Arch with PipeWire) |
| Compiler | GCC 13+ or Clang 17+ (C++20) |
| CMake | 3.21+ |
| Qt | 6.5+ (Widgets, Multimedia, MultimediaWidgets) |
| FFmpeg | 4.0+ (provides `ffmpeg` and `ffprobe` on PATH) |

### Installing dependencies on Arch

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-multimedia ffmpeg
```

The Qt FFmpeg multimedia plugin is bundled with `qt6-multimedia` on current
Arch, no extra package needed.

### Installing from the AUR

If you just want to *use* the app on Arch, install it from the AUR with your
favorite helper:

```bash
# stable, tagged release
yay -S vtrim

# or, latest git master
yay -S vtrim-git
```

> The AUR package is named `vtrim` (not `video-trimmer`) because the bare
> `video-trimmer` name is owned by the unrelated GTK4 GNOME Circle app
> shipped in `extra/`. Picking a distinct binary name lets the two coexist
> on the same system. The user-facing menu entry still reads
> *"Video Trimmer"*.

Both PKGBUILDs live in [`dist/aur/`](dist/aur/) in this repo for reference;
see `dist/aur/README.md` for how new releases are pushed to the AUR.

## Building

```bash
cmake -S . -B build
cmake --build build -j
```

Run it:

```bash
./build/vtrim
```

For development, `compile_commands.json` is written into `build/` so
`clangd`/your editor can index the project (symlink it to the project root
if your editor expects it there).

### Installing (per-user, no sudo)

The recommended way to actually *use* the app is a per-user install under
`~/.local`, which puts the binary on your `PATH` and registers a
`.desktop` entry so it shows up in your application launcher (Walker, wofi,
GNOME Activities, KDE krunner, etc.):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j
cmake --install build
```

This installs:

- `~/.local/bin/vtrim`
- `~/.local/share/applications/vtrim.desktop`

Now just run `vtrim` from any shell, or launch *Video Trimmer* from
your app menu. Because the process runs as **you**, exported clips are
written as your user and land in any directory you can normally write to.

> **Don't run the app with `sudo`.** It works, but the FFmpeg child writes
> the output as `root`, which is what causes the "I can't save the trimmed
> file in most directories" symptom. `QSettings` also ends up under
> `/root/.config` instead of `~/.config`, so your recent files and pinned
> audio device won't follow you across sessions.

To uninstall, delete the two files above (or run `cat
build/install_manifest.txt | xargs rm`).

### Installing system-wide

Same flow, just point at a system prefix and run the install step with
`sudo`:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
sudo cmake --install build
```

> **Don't** use a raw `cmake --install` to `/usr/local` if you also have the
> AUR `vtrim` package installed — they will both ship a `vtrim` binary and a
> `vtrim.desktop`, and `/usr/local/share/applications/vtrim.desktop` shadows
> `/usr/share/applications/vtrim.desktop` (XDG ordering), giving you two
> identically-named menu entries running different builds. Use the
> `refresh` workflow below instead.

### Dev workflow: `cmake --build build --target refresh` (Arch only)

If you're iterating on the code on an Arch box and want `/usr/bin/vtrim` to
always reflect your current working tree — without ever ending up with stale
copies in `~/.local/bin` or `/usr/local/bin` that pacman doesn't know about —
use the bundled `refresh` target:

```bash
cmake -S . -B build
cmake --build build --target refresh
```

Under the hood this runs [`dev/refresh-install.sh`](dev/refresh-install.sh)
which drives the [`dist/aur/vtrim-local/PKGBUILD`](dist/aur/vtrim-local/PKGBUILD):

1. `makepkg -sf` builds a **Release** binary from the current workspace and
   packages it as `vtrim-local`. A `pkgver()` function stamps the version
   as `<project-version>.local.<UTC timestamp>` so every refresh is unique
   and pacman is willing to reinstall it.
2. If `vtrim` or `vtrim-git` is currently installed, the script removes it
   first (they all own `/usr/bin/vtrim`, so they conflict by design — and
   `makepkg --noconfirm` can't auto-accept pacman's "Remove vtrim? [y/N]"
   prompt because the default answer is N).
3. `sudo pacman -U` installs the freshly-built `vtrim-local` package.
   Because `vtrim-local` declares `provides=(vtrim)`, anything that ever
   depended on `vtrim` is still satisfied.

You'll be prompted for your sudo password once during the install step. To
switch back to the upstream package: `sudo pacman -S vtrim` (or
`vtrim-git`); that automatically replaces `vtrim-local` (same `provides`).

## Usage

1. **Open a media file** with `File → Open Media…`, `Ctrl+O`, or by dragging
   a video or audio file onto the window. Recognized audio formats include
   `.mp3`, `.m4a`, `.aac`, `.ogg`, `.opus`, `.flac`, `.wav`, and `.wma`.
2. **Set In/Out points**: use the `Set In` / `Set Out` buttons in the
   transport bar, the `I` / `O` keys, drag the handles on the timeline, or
   type the exact timestamp directly into the **In:** / **Out:** fields next
   to those buttons (e.g. `00:12:25.500`, `12:25`, or `90` for 90 s) and
   press Enter.
3. **Preview** with the Play button or Space. Playback is constrained to
   `[In, Out]`; toggle `Loop` to repeat the selection forever instead of
   stopping at Out.
4. **Crop to the selection** (optional, iterative) with `File → Crop to
   Selection`, `Ctrl+K`, or the *Crop* button in the transport bar. This
   replaces the working clip with just the `[In, Out]` fragment so you can
   keep tightening the trim against the smaller clip. The cut is stream
   copy (lossless, snaps to the source's nearest preceding keyframe) and
   the original file on disk is left alone — only the in-app player is
   pointed at a temp file that gets cleaned up on Close or quit. Use
   `File → Close` (or open a different file) to drop the edited state
   and forget the temp file.
5. **Cut the selection out** (also optional, iterative) with `File → Cut
   Selection`, `Ctrl+Shift+K`, or the *Cut* button. The opposite of Crop:
   it removes `[In, Out]` and stitches the rest back together so you can
   strip out multiple unwanted regions in sequence. Same stream-copy /
   keyframe-snap caveats as Crop apply at the seam between the two
   surviving halves; same temp-file lifecycle.
6. **Export** with `File → Export Trimmed…` or `Ctrl+E`. Pick the output
   path and trim mode in the dialog, then watch the progress. For
   audio-only sources you also get an **Audio Format** dropdown — see the
   *Audio output formats* table below — that controls the output codec and
   extension in Precise mode. Fast (stream-copy) mode always preserves the
   source codec/container, so the format picker is disabled there.

### Keyboard shortcuts

| Key | Action |
|---|---|
| `Ctrl+O` | Open media (video or audio) |
| `Ctrl+K` | Crop working clip to the current `[In, Out]` |
| `Ctrl+Shift+K` | Cut the current `[In, Out]` out of the working clip |
| `Ctrl+E` | Export trimmed media |
| `Ctrl+W` | Close current media |
| `Ctrl+Q` | Quit |
| `Space` | Play / pause selection |
| `J` / `K` / `L` | Rewind 1 s / pause / play selection |
| `Left` / `Right` | Nudge playhead ±1 ms |
| `Shift +Left/Right` | Nudge playhead ±10 ms |
| `Ctrl +Left/Right` | Nudge playhead ±100 ms |
| `Ctrl+Shift +Left/Right` | Nudge playhead ±1000 ms |
| `Home` / `End` | Jump to start / end |
| `I` / `O` | Set In / Out at the current playhead |
| `[` / `]` | Nudge **In** point − / + (with Shift/Ctrl modifiers as above) |
| `Alt+[` / `Alt+]` | Nudge **Out** point − / + (with the same modifiers) |

### Audio output

Qt 6 + PipeWire can sometimes recreate the audio stream during playback (on
pause/resume or seeks), which loses any per-stream routing you set in
`pavucontrol` / Helvum. To get reliable, sticky routing, **pin a device** in
`Audio → Output Device`. The choice persists across sessions.

If you prefer to manage routing through your system mixer, leave the menu at
`Follow System Default` and route in `pavucontrol` after starting playback.

#### Why we force the PulseAudio backend

The app sets `QT_AUDIO_BACKEND=pulseaudio` in `main()` before constructing
`QApplication`. Qt 6.10 introduced a native PipeWire audio backend that, on
device enumeration, asks every `Audio/Sink` node for `SPA_PARAM_EnumFormat`
and silently drops any node that doesn't reply with a format. Bluetooth A2DP
sinks only negotiate a format once a stream connects, so they always get
dropped — meaning BT headphones never appear in `Audio → Output Device`.
Falling back to the older PulseAudio backend (which talks to `pipewire-pulse`
and lists every sink the daemon exposes) restores them. If you want to
opt out and try the native PipeWire backend anyway, set the env var
yourself before launching: `QT_AUDIO_BACKEND=pipewire ./build/vtrim`.

### Trim modes

| Mode | Speed | Accuracy | Notes |
|---|---|---|---|
| Precise (default) — video | Slow (re-encode) | Millisecond-exact | Output is MP4 / H.264 (CRF 18) / AAC 192 kb/s, with `+faststart` |
| Precise (default) — audio | Slow (re-encode) | Millisecond-exact | Output codec/container picked from **Audio Format** (see table below) |
| Fast (stream copy)        | Near-instant     | Snaps to nearest keyframe of the source | Output container defaults to the source container, since `-c copy` doesn't always survive remuxing across containers |

#### Audio output formats

Audio-only sources in Precise mode let you pick one of:

| Format    | Extension | Encoder        | Bitrate / quality |
|---|---|---|---|
| AAC       | `.m4a`    | `aac`          | 192 kb/s          |
| MP3       | `.mp3`    | `libmp3lame`   | 192 kb/s          |
| Opus      | `.opus`   | `libopus`      | 128 kb/s          |
| Vorbis    | `.ogg`    | `libvorbis`    | `-q:a 5` (~160 kb/s VBR) |
| FLAC      | `.flac`   | `flac`         | lossless          |
| WAV (PCM) | `.wav`    | `pcm_s16le`    | lossless, 16-bit  |

The dialog picks a default that matches the source codec (`.mp3` in →
`.mp3` out, `.flac` in → `.flac` out, etc.) so trimming a file in place
doesn't silently change its format.

> The non-built-in encoders (`libmp3lame`, `libvorbis`, `libopus`) require
> an `ffmpeg` build with those external libraries enabled. Arch's `extra/ffmpeg`
> ships them all; some minimal builds may not. If your build is missing one,
> the export will fail and the error dialog will surface ffmpeg's complaint.

The exact commands run are:

```bash
# Precise (video source)
ffmpeg -hide_banner -y -nostdin \
  -ss <inMs> -to <outMs> -i <input> \
  -map 0 -c:v libx264 -preset medium -crf 18 \
  -c:a aac -b:a 192k -movflags +faststart \
  -progress pipe:1 -nostats <output>

# Precise (audio-only source); <audio-codec-args> comes from the picked
# format, e.g. `-c:a aac -b:a 192k`, `-c:a libmp3lame -b:a 192k`,
# `-c:a libopus -b:a 128k`, `-c:a libvorbis -q:a 5`, `-c:a flac`, or
# `-c:a pcm_s16le`.
ffmpeg -hide_banner -y -nostdin \
  -ss <inMs> -to <outMs> -i <input> \
  -map 0 -vn <audio-codec-args> \
  -progress pipe:1 -nostats <output>

# Fast (any source) - also what Crop uses
ffmpeg -hide_banner -y -nostdin \
  -ss <inMs> -to <outMs> -i <input> \
  -map 0 -c copy -avoid_negative_ts make_zero \
  -progress pipe:1 -nostats <output>
```

`-ss` is placed *before* `-i` so input seeking is fast, and (since FFmpeg
4.0) still frame-accurate when re-encoding.

Cut is a three-step stream-copy pipeline that extracts the surviving
halves and concatenates them through the `concat` demuxer. Each step is
stream copy, so the whole thing typically completes in well under a
second:

```bash
# 1. Extract the [0, In] head (skipped if In == 0)
ffmpeg -hide_banner -y -nostdin \
  -ss 00:00:00.000 -to <inMs> -i <input> \
  -map 0 -c copy -avoid_negative_ts make_zero \
  -progress pipe:1 -nostats <pre>

# 2. Extract the [Out, end] tail (skipped if Out >= duration)
ffmpeg -hide_banner -y -nostdin \
  -ss <outMs> -i <input> \
  -map 0 -c copy -avoid_negative_ts make_zero \
  -progress pipe:1 -nostats <post>

# 3. Stitch them with the concat demuxer. <list> is a tiny text file:
#      file '<pre>'
#      file '<post>'
ffmpeg -hide_banner -y -nostdin \
  -f concat -safe 0 -i <list> \
  -map 0 -c copy \
  -progress pipe:1 -nostats <output>
```

When the cut keeps only a single half (`In == 0` or `Out == duration`)
the pipeline collapses to step 2 or step 1 alone, writing straight to
the final output and skipping the concat pass entirely.

## Project layout

```
video-trimmer/
├── CMakeLists.txt
├── README.md
├── packaging/
│   └── vtrim.desktop.in               # freedesktop launcher entry (templated)
└── src/
    ├── main.cpp
    ├── MainWindow.{h,cpp}             # menus, layout, drag-n-drop, shortcuts, settings
    ├── TrimDialog.{h,cpp}             # export options dialog
    ├── VideoSession.{h,cpp}           # current file + In/Out + media metadata
    ├── player/
    │   ├── PlayerWidget.{h,cpp}       # QMediaPlayer + QAudioOutput + QVideoWidget
    │   └── TransportBar.{h,cpp}       # play/pause, time labels, volume, loop, set In/Out
    ├── timeline/
    │   └── TimelineWidget.{h,cpp}     # custom-painted ruler, playhead, In/Out handles
    ├── ffmpeg/
    │   ├── MediaInfo.h                # POD result struct
    │   ├── FfprobeRunner.{h,cpp}      # async ffprobe → JSON → MediaInfo
    │   ├── FfmpegTrimRunner.{h,cpp}   # async ffmpeg trim with progress
    │   └── FfmpegCutRunner.{h,cpp}    # multi-step async ffmpeg cut + concat
    └── util/
        └── TimeFormat.{h,cpp}         # ms ↔ HH:MM:SS.mmm and ms ↔ frame
```

## Architecture

```
                 ┌──────────────┐
                 │  MainWindow  │  menus + layout + key handling
                 └──────┬───────┘
        ┌───────────────┼─────────────────────────────────┐
        ▼               ▼                                 ▼
 ┌──────────────┐ ┌──────────────┐                ┌────────────────┐
 │ PlayerWidget │ │ TimelineWid. │                │ VideoSession   │
 │ QMediaPlayer │ │ paint+mouse  │                │ path + metadata│
 │ QAudioOutput │ │ in/out drag  │                │ + In/Out state │
 │ QVideoWidget │ └──────┬───────┘                └─────┬──────────┘
 └──────┬───────┘        │ seekRequested,                │ openFile
        │ position,      │ inOutChanged                  │
        │ duration       ▼                               ▼
        ▼          ┌──────────────┐                ┌──────────────┐
 ┌──────────────┐  │ TransportBar │                │ FfprobeRunner│
 │  audio +     │  │ play / pause │                │ QProcess +   │
 │  video out   │  │ loop / vol   │                │ JSON parse   │
 └──────────────┘  └──────────────┘                └──────────────┘

                                                   ┌──────────────────┐
                            File→Export… ────────► │ FfmpegTrimRunner │
                            File→Crop  ──────────► │ QProcess +       │
                                                   │ -progress parser │
                                                   └──────────────────┘
                                                   ┌──────────────────┐
                            File→Cut   ──────────► │ FfmpegCutRunner  │
                                                   │ pre + post +     │
                                                   │ concat demuxer   │
                                                   └──────────────────┘
```

Everything talks via Qt signals/slots; there are no manual threads. `QProcess`
handles `ffmpeg` and `ffprobe` asynchronously on the GUI thread.

## Design decisions

- **`qint64` milliseconds everywhere.** All time values across signals,
  slots, and storage are `qint64` ms. Frame numbers are derived from the
  framerate reported by `ffprobe` but are never the source of truth.
- **No libav linkage.** We talk to FFmpeg only through subprocess + the
  CLI's `-progress pipe:1` machine-readable output. This keeps the build
  trivial and avoids the licensing complexity of linking against `libav*`.
- **Custom timeline instead of `QSlider`.** A trimmer needs *two* draggable
  handles plus a playhead, plus a shaded selection region; `QSlider` only
  has one handle, so a small custom `QWidget` with `paintEvent` and mouse
  events is simpler than fighting the stock widget.
- **Trim-aware Play.** The Play action always operates on the current
  selection — if the playhead is outside `[In, Out]` it jumps to In, and
  playback stops (or loops, if enabled) at Out. Manual scrubbing on the
  timeline is unconstrained so you can still inspect frames outside the
  selection.
- **One UI for video and audio.** Audio-only files reuse the same timeline,
  In/Out fields, transport bar, and keyboard shortcuts as video. The only
  audio-specific concessions are: the player surface swaps to a static
  speaker placeholder (instead of staying perpetually black), the export
  dialog hides the Frame Rate group and shows an **Audio Format** picker
  in its place, and the precise-mode FFmpeg pipeline drops the H.264 /
  `-r` / `+faststart` flags and uses the per-format encoder args from the
  picker.

## Troubleshooting

### Video plays but no audio, or audio routes to the wrong device

This is almost always a Qt 6 + PipeWire interaction. Pin your preferred
device under `Audio → Output Device`; the choice survives across pause/play
and across app restarts.

### Export produces a black first frame in Fast mode

`-c copy` snaps to the nearest preceding keyframe of the source, which can
land before your In point. Switch to **Precise** mode for ms-accurate
boundaries, or move your In point slightly forward to land on a keyframe.

### `ffprobe`/`ffmpeg` not found

Ensure FFmpeg is installed and on PATH:

```bash
sudo pacman -S ffmpeg
which ffmpeg ffprobe
```

The app uses `QStandardPaths::findExecutable` to locate them.

## License

This project is released under the [MIT License](LICENSE). FFmpeg, when
installed, has its own license — we only invoke it as a subprocess, we do
not link against `libav*`.
