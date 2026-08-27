<div align="center">

# Media Framework for HeavenOS

### *A video-only bridge from NEOS to the real multimedia world*

Built on [HeavenOS/NEOS](https://github.com/Thinkman405/HeavenOS) — consumed here as a git
submodule pinned to a tagged release, never `main`.

[![HeavenOS](https://img.shields.io/badge/HeavenOS-v0.3.0-6C5CE7?style=for-the-badge)](https://github.com/Thinkman405/HeavenOS/releases/tag/v0.3.0)
[![Scope](https://img.shields.io/badge/scope-video%20only-00B4D8?style=for-the-badge)](#scope--video-only)
[![Target](https://img.shields.io/badge/target-GStreamer%20element-2EC4B6?style=for-the-badge)](#what-this-is-for)
[![Status](https://img.shields.io/badge/status-built%20%26%20verified-2EC4B6?style=for-the-badge)](#status)

</div>

<br>

> `neoscrystallize` is a real, working GStreamer plugin — registers correctly under
> `gst-inspect-1.0`, and a real `gst-launch-1.0` pipeline through it has produced genuine
> crystallisation data, not a placeholder. Built and verified in WSL2/Ubuntu, since the official
> Windows GStreamer SDK needs administrator elevation this environment couldn't grant.

<br>

## Table of contents

- [Scope — video only](#scope--video-only)
- [What this is for](#what-this-is-for)
- [Why GStreamer, not FFmpeg or VLC](#why-gstreamer-not-ffmpeg-or-vlc)
- [Status](#status)
- [Building](#building)

<br>

## Scope — video only

This repo is deliberately scoped to **video**, not image/audio/text. NEOS's `crystallisation`
crate has all four media pipelines — and [HeavenOS's own `media_ffi` bridge now exposes all
four](https://github.com/Thinkman405/HeavenOS#subsystems) across a real C ABI — but this framework
exposes only the volumetric-time-crystal (video) one. A stated choice, not a limitation of the
underlying core.

<br>

## What this is for

Bridges NEOS's real video crystallisation
(`crystallisation::timecrystal::VolumetricTimeCrystal::crystallise_video`, via
`media_ffi_crystallise_video` in the core repo's C FFI bridge) into a real, dynamically-loadable
**GStreamer element** — a `.dll`/`.so` any GStreamer-based application can load at runtime, no
rebuild required.

<br>

## Why GStreamer, not FFmpeg or VLC

The three ecosystems originally in scope don't actually offer the same kind of integration —
checked before committing to any of them, not assumed:

| Ecosystem | What a real integration would require | Verdict |
|---|---|---|
| **FFmpeg** | No stable, dynamically-loadable out-of-tree filter ABI in mainline `libavfilter` — a filter means a custom-built FFmpeg fork, not a drop-in plugin | Not pursued (for now) |
| **VLC** | Doesn't use GStreamer at all — its own separate plugin architecture (VLC modules), built on `libavcodec` internally | A distinct, later target if still wanted |
| **GStreamer** | A genuine, stable, dynamically-loadable out-of-tree plugin ABI — exactly what this repo needs | **Chosen** |

GStreamer reaches GStreamer-based applications (GNOME's Totem, various Linux and embedded media
tools) — not VLC specifically, despite the name similarity in casual use.

<br>

## Status

| Piece | State |
|---|---|
| Video C FFI bridge (`media_ffi_crystallise_video`) | ✅ Built and verified — opaque handle, panic-guarded, correct ownership, checked against both a Rust suite and a real, independently compiled C program (on both MSVC/Windows and `gcc`/Linux) |
| GStreamer development environment | ✅ Available — GStreamer 1.28.2 + `gcc`/`pkg-config`/`build-essential` in WSL2/Ubuntu |
| `neoscrystallize` GStreamer element | ✅ **Built and verified** — real registration under `gst-inspect-1.0` (correct `GstBaseSink` ancestry, `GRAY8` caps, every property present), and a real `gst-launch-1.0` pipeline producing genuine crystallisation data, not a placeholder. Full detail: [`CONTEXT.md`](CONTEXT.md). |

**The element is built as a Linux `.so`, not a Windows `.dll`** — a deliberate consequence of the
WSL route, not a compromise: the GStreamer-based applications this element actually targets
(GNOME's Totem, various Linux/embedded tools) are predominantly Linux anyway.

**One real gap the design missed until an actual pipeline was run**: raw 8-bit video pixel values
overflow the Howard-Comma quantisable ceiling with no rescaling — `media_ffi` applies none of its
own, by design, so a `scale` property (default `3.0e-9`, matching this workspace's own real video
fixtures) was added to the element itself. See `CONTEXT.md` for the exact error this closed.

Submodule pinned to `v0.3.0`.

<br>

## Building

```bash
git submodule update --init --recursive
cd vendor/heavenos/neos && cargo build -p media-ffi
cd ../../../src && ./build.sh
```

Then, with a GStreamer install available (see [Status](#status) if it isn't yet):

```bash
GST_PLUGIN_PATH=src gst-inspect-1.0 neoscrystallize
GST_PLUGIN_PATH=src gst-launch-1.0 -e videotestsrc num-buffers=20 pattern=ball ! videoconvert ! \
  video/x-raw,format=GRAY8,width=8,height=8,framerate=30/1 ! neoscrystallize output-path=result.txt
```

<br>

<div align="center">

*Part of the [HeavenOS](https://github.com/Thinkman405/HeavenOS) family — core stays untouched,
every product gets its own repo.*

</div>
