# Video Trimmer

A simple, millisecond-precision desktop video trimmer for Linux, written in
C++ with Qt 6 and FFmpeg. Designed to be the smallest sensible app that lets
you open a video, mark exact In/Out points, preview the selection, and export
a trimmed copy.

Primary target is Arch Linux. It should also work on any modern Linux distro
that ships Qt 6.5+ and FFmpeg.

## Features

- Embedded video preview powered by `QMediaPlayer` (Qt's FFmpeg backend).
- Custom timeline widget with a draggable playhead and **In / Out handles**
  that show a shaded selection.
- **Millisecond-precision** time handling everywhere; current/total time is
  always displayed as `HH:MM:SS.mmm`.
- Keyboard nudging at 1 / 10 / 100 / 1000 ms granularity for both the
  playhead and the In/Out points.
- "Play within selection" semantics: pressing Play (button, Space, or `L`)
  always starts at the In point and stops at the Out point.
- Toggleable **Loop** for the trim selection — when on, playback restarts
  from In whenever it reaches Out.
- Two export modes:
  - **Precise** (default) — re-encodes to MP4 (H.264 + AAC), millisecond-
    accurate boundaries.
  - **Fast** — `ffmpeg -c copy`, near-instant but snaps to the nearest
    keyframe of the source.
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

## Building

```bash
cmake -S . -B build
cmake --build build -j
```

Run it:

```bash
./build/video-trimmer
```

For development, `compile_commands.json` is written into `build/` so
`clangd`/your editor can index the project (symlink it to the project root
if your editor expects it there).

## Usage

1. **Open a video** with `File → Open Video…`, `Ctrl+O`, or by dragging a
   file onto the window.
2. **Set In/Out points**: use the `Set In` / `Set Out` buttons in the
   transport bar, the `I` / `O` keys, or drag the handles on the timeline.
3. **Preview** with the Play button or Space. Playback is constrained to
   `[In, Out]`; toggle `Loop` to repeat the selection forever instead of
   stopping at Out.
4. **Export** with `File → Export Trimmed…` or `Ctrl+E`. Pick the output
   path and trim mode in the dialog, then watch the progress.

### Keyboard shortcuts

| Key | Action |
|---|---|
| `Ctrl+O` | Open video |
| `Ctrl+E` | Export trimmed video |
| `Ctrl+W` | Close current video |
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
yourself before launching: `QT_AUDIO_BACKEND=pipewire ./build/video-trimmer`.

### Trim modes

| Mode | Speed | Accuracy | Notes |
|---|---|---|---|
| Precise (default) | Slow (re-encode) | Millisecond-exact | Output is MP4 / H.264 (CRF 18) / AAC 192 kb/s, with `+faststart` |
| Fast (stream copy) | Near-instant | Snaps to nearest keyframe of the source | Output container defaults to the source container, since `-c copy` doesn't always survive remuxing across containers |

The exact commands run are:

```bash
# Precise
ffmpeg -hide_banner -y -nostdin \
  -ss <inMs> -to <outMs> -i <input> \
  -map 0 -c:v libx264 -preset medium -crf 18 \
  -c:a aac -b:a 192k -movflags +faststart \
  -progress pipe:1 -nostats <output>

# Fast
ffmpeg -hide_banner -y -nostdin \
  -ss <inMs> -to <outMs> -i <input> \
  -map 0 -c copy -avoid_negative_ts make_zero \
  -progress pipe:1 -nostats <output>
```

`-ss` is placed *before* `-i` so input seeking is fast, and (since FFmpeg
4.0) still frame-accurate when re-encoding.

## Project layout

```
video-trimmer/
├── CMakeLists.txt
├── README.md
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
    │   └── FfmpegTrimRunner.{h,cpp}   # async ffmpeg trim with progress
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
                                                   │ QProcess +       │
                                                   │ -progress parser │
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

Source code is in this repository; choose and add the license you prefer
before distributing. FFmpeg, when installed, has its own license.
