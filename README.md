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
(`crystallisation::timecrystal::VolumetricTimeCrystal::crystallise_video`,
via `media_ffi_crystallise_video` in the core repo's C FFI bridge) into a
real, dynamically-loadable **GStreamer element** — a `.dll`/`.so` any
GStreamer-based application can load at runtime with no rebuild.

That target was chosen deliberately, not assumed, after checking how the
three originally-named ecosystems (FFmpeg, GStreamer, VLC) actually work:
FFmpeg's mainline `libavfilter` has **no stable, dynamically-loadable
out-of-tree filter ABI** — a real FFmpeg filter means a custom-built
FFmpeg fork, not a drop-in plugin. **VLC does not use GStreamer** — it has
its own separate plugin architecture (VLC modules), built on `libavcodec`
internally. GStreamer is the one of the three with a genuine out-of-tree
plugin system, so it's the first (and, for now, only) target — it reaches
GStreamer-based applications (GNOME's Totem, various Linux/embedded media
tools), not VLC specifically. A VLC module would be a separate, later
target if VLC integration is still wanted.

## Status

**The video FFI bridge exists and is verified; no GStreamer element yet.**
`vendor/heavenos`'s `media_ffi` crate exposes
`media_ffi_crystallise_video` and accessors across a real C ABI — opaque
handle, panic-guarded, correct ownership — verified against both a Rust
test suite and a real, independently MSVC-compiled C program
(`vendor/heavenos/neos/media_ffi/ffi_test/main.c`).

**Blocked on the GStreamer development SDK**, currently: it needs
administrator elevation to install, which the development environment this
was built in cannot grant (no interactive session to approve the UAC
prompt). Not yet resolved.

The full design for the element itself — base class, caps, properties,
lifecycle, verification plan — is written up in
[`CONTEXT.md`](CONTEXT.md), ready to implement the moment the SDK is
available, so no design work blocks on the install.

Submodule pinned to `v0.2.0`.

## Building

```bash
git submodule update --init --recursive
```

Then see HeavenOS's own `neos/` workspace for how the underlying Rust
crates build and test.
