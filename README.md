# Media Framework for HeavenOS

A video-only media framework built on [HeavenOS/NEOS](https://github.com/Thinkman405/HeavenOS)
(consumed here as a submodule, pinned to a tagged release — see
[`vendor/heavenos/_templates/product-repo/CONTEXT.md`](vendor/heavenos/_templates/product-repo/CONTEXT.md)
for the strategy).

## Scope — video only

This repo is deliberately scoped to **video**, not image/audio/text. NEOS's
`crystallisation` crate has all four media pipelines, but this framework
exposes only the volumetric-time-crystal (video) one — a stated choice, not
a limitation of the underlying core.

## What this is for

Bridges NEOS's real video crystallisation
(`crystallisation::timecrystal::VolumetricTimeCrystal::crystallise_video`)
into the multimedia plugin ecosystems real applications already use, so
existing video software can consume NEOS-crystallised video without
knowing anything about HeavenOS itself:

- an **FFmpeg filter**, usable by anything built on `libavfilter`
- a **GStreamer element/plugin**, usable by anything built on the
  GStreamer pipeline model (VLC, and other video/editing applications)

## Status

**Not yet built.** Current state: the HeavenOS submodule is wired in,
pinned to `v0.1.0`. HeavenOS's existing C FFI bridge
(`vendor/heavenos/neos/media_ffi`) currently covers only the **image**
pipeline — extending it with the equivalent video bridge, verified the
same way (real ownership discipline, a panic boundary, and a genuine,
independently-compiled C program proving the ABI actually works) is the
next concrete step, before either plugin gets written.

## Building

```bash
git submodule update --init --recursive
```

Then see HeavenOS's own `neos/` workspace for how the underlying Rust
crates build and test.
