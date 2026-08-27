<div align="center">

# Media Framework for HeavenOS

### *A video-only bridge from NEOS to the real multimedia world*

Built on [HeavenOS/NEOS](https://github.com/Thinkman405/HeavenOS) — consumed here as a git
submodule pinned to a tagged release, never `main`.

[![HeavenOS](https://img.shields.io/badge/HeavenOS-v0.2.0-6C5CE7?style=for-the-badge)](https://github.com/Thinkman405/HeavenOS/releases/tag/v0.2.0)
[![Scope](https://img.shields.io/badge/scope-video%20only-00B4D8?style=for-the-badge)](#scope--video-only)
[![Target](https://img.shields.io/badge/target-GStreamer%20element-2EC4B6?style=for-the-badge)](#what-this-is-for)
[![Status](https://img.shields.io/badge/status-blocked%20on%20SDK%20install-FF6B6B?style=for-the-badge)](#status)

</div>

<br>

> The video FFI bridge this framework builds on is real and verified. The GStreamer element itself
> is fully designed and ready to implement — blocked only on installing a development SDK in the
> current environment, not on any open design question.

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
| Video C FFI bridge (`media_ffi_crystallise_video`) | ✅ Built and verified — opaque handle, panic-guarded, correct ownership, checked against both a Rust suite and a real, independently MSVC-compiled C program |
| `neoscrystallize` GStreamer element design | ✅ Complete — base class, caps, properties, EOS-driven lifecycle, verification plan, all written up in [`CONTEXT.md`](CONTEXT.md) |
| `neoscrystallize` implementation | ⏳ Blocked — see below |

**Blocked on the GStreamer development SDK.** It needs administrator elevation to install, which
the development environment this was built in cannot grant (no interactive session to approve the
UAC prompt) — `winget install` reports success without anything actually landing on disk. Not yet
resolved. No implementation work is waiting on a design decision; only on the SDK becoming
installable.

Submodule pinned to `v0.2.0`.

<br>

## Building

```bash
git submodule update --init --recursive
```

Then see HeavenOS's own `neos/` workspace for how the underlying Rust crates build and test.

<br>

<div align="center">

*Part of the [HeavenOS](https://github.com/Thinkman405/HeavenOS) family — core stays untouched,
every product gets its own repo.*

</div>
