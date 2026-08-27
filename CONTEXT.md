# Media Framework for HeavenOS — the `neoscrystallize` GStreamer element

**Built and verified** — `src/gstneoscrystallize.c`, a real, dynamically-loadable
GStreamer plugin. Registers correctly under `gst-inspect-1.0` and has run a
real `gst-launch-1.0` pipeline end to end, producing genuine crystallisation
data (see "Built and verified" at the bottom). The design below is what was
actually implemented, not a proposal. Every `media_ffi` signature referenced
here is a real, already-verified function in `vendor/heavenos/neos/media_ffi`
(opaque handle, panic-guarded, ownership correct, proven against a real
independent C program).

Built on Linux, via WSL2/Ubuntu — the official Windows GStreamer SDK needs
administrator elevation this development environment couldn't grant, and
WSL sidesteps that entirely. Ships as a Linux `.so`, which fits its actual
target audience (GStreamer-based Linux apps) better than a Windows `.dll`
would have anyway.

## What kind of element this is, and why

`crystallise_video` is a **batch** operation — the underlying algorithm
needs the whole frame sequence's energy time series before its FFT and
quantisation work can even start (see `media_ffi`'s own module docs on
why the FFI bridge takes one flat buffer, not per-frame calls). There is
no streaming/incremental shape to preserve here, which settles the actual
GStreamer element type: this is a **sink**, not a transform. It consumes
video and produces a summary result, the same shape as `fakesink`/
`filesink`, not a video-in-video-out element like `videoconvert`.

**Base class:** `GstBaseSink`.
**Element name:** `neoscrystallize`.

## Sink pad caps

```
video/x-raw, format=GRAY8
```

Greyscale only, matching `crystallisation`'s own convention throughout
(`decode_ppm` reads P5 greyscale directly; `PixelGrid` is one `f64` per
pixel, no channels). A real pipeline converts color video upstream:

```
... ! videoconvert ! video/x-raw,format=GRAY8 ! neoscrystallize
```

Pixel conversion is `byte as f64 * scale` (see `scale` below) — `media_ffi`
itself applies no rescaling (range 0-255, matching
`crystallisation::codec::decode_ppm`'s own real behaviour, `body[off] as
f64`), not assumed.

## Properties

| Name | Type | Direction | Purpose |
|---|---|---|---|
| `tau` | `uint`, default `3` | read/write | Takens embedding delay, passed straight to `media_ffi_crystallise_video`. |
| `scale` | `double`, default `3.0e-9` | read/write | Multiplied into every raw pixel byte before crystallisation — see below; not in the original design, added after real testing found it necessary. |
| `output-path` | `string`, optional | read/write | If set, write a plain-text summary here when crystallisation completes. |
| `node-count` | `uint64` | read-only | Set after EOS from `media_ffi_video_result_node_count`. |
| `input-energy` | `double` | read-only | Set after EOS from `media_ffi_video_result_input_energy`. |
| `fundamental-hz` | `double` | read-only | Set after EOS from `media_ffi_video_result_fundamental_hz`. |
| `energy-conserving` | `boolean` | read-only | Set after EOS from `media_ffi_video_result_is_energy_conserving`. |

**`scale` — a real gap the original design missed, found by actually running
a pipeline, not by re-reading the spec.** `media_ffi_crystallise_video`
applies no rescaling of its own — per `_mkb/timecrystal.md` §5.3, that is
the caller's responsibility, the same way `neos/src/main.rs`'s own demo
rescales its embedded frames before crystallising them. The first real
`gst-launch-1.0` run through this element, before `scale` existed, hit
exactly that: raw `videotestsrc` GRAY8 output (0-255) overflowed the
Howard-Comma quantisable ceiling immediately (`signal needs 6.28e47 C_H
quanta; only 9.0e15 are exactly countable`) — a correct, honest refusal,
but one that made the element unusable with *any* real 8-bit video without
a rescale step nothing in the pipeline provided. `scale`'s default,
`3.0e-9`, matches the value this workspace's own real video fixtures
already use elsewhere; a caller whose data is already pre-scaled sets
`scale=1.0`.

## Internal state

- A growing `GByteArray` accumulating every frame's raw pixel bytes, in
  arrival order — exactly the frame-major layout `media_ffi_crystallise_video`
  expects once cast to `f64`.
- `width`, `height` — captured once from negotiated caps in `set_caps()`,
  not re-read per buffer.
- `frame_rate` — from the caps' `framerate` fraction (`numerator / denominator
  as f64`).

## Lifecycle

1. **`start()`** — reset the accumulator to empty.
2. **`set_caps()`** — parse and store `width`, `height`, `frame_rate` from
   the negotiated caps. Refuse (return `FALSE`) if the format isn't
   `GRAY8` — caps negotiation should already prevent this given the pad
   template above, but a real element checks explicitly rather than
   trusting negotiation alone.
3. **`render(buffer)`** — map the incoming `GstBuffer` read-only, append
   its bytes to the accumulator. No FFI call here — this is pure
   accumulation, matching `crystallise_video`'s batch nature.
4. **EOS** (via the `event()` vfunc catching `GST_EVENT_EOS`, not `stop()`
   — `stop()` alone doesn't distinguish "stream finished cleanly" from
   "pipeline torn down early"):
   - `frame_count = accumulated_len / (width * height)`.
   - Cast every accumulated byte to `f64`, call
     `media_ffi_crystallise_video(frames, frame_count, width, height,
     frame_rate, tau)`.
   - Check `media_ffi_video_result_is_ok`. On success, read
     `node_count`/`input_energy`/`fundamental_hz`/`is_energy_conserving`
     into the element's own properties; on failure, read
     `media_ffi_video_result_error_message` and post a `GST_MESSAGE_ERROR`
     on the bus instead.
   - If `output-path` is set, write a plain-text summary there.
   - Post a custom `GST_MESSAGE_APPLICATION` on the bus (e.g. structure
     name `neoscrystallize-result`) so pipeline code can react to
     completion without polling properties.
   - Free the result handle via `media_ffi_video_result_free` — exactly
     once, matching the bridge's own documented ownership contract.

## Implementation language

C, against `gstreamer-1.0`/`gstbase-1.0`/`gstreamer-video-1.0`'s own
headers, linking directly against `media_ffi`'s built shared library the
same way `vendor/heavenos/neos/media_ffi/ffi_test/main.c` already does —
no new language or binding layer needed, since `media_ffi.h` already
exists and is already proven correct against a real, independently
compiled C program.

## Building

```bash
cd vendor/heavenos/neos && cargo build -p media-ffi   # build the FFI bridge first
cd ../../../src && ./build.sh                          # build the plugin
```

`build.sh` links with an `$ORIGIN`-relative `-rpath` so the resulting
`libgstneoscrystallize.so` finds `libmedia_ffi.so` at runtime regardless of
the caller's working directory — a real, previously-hit bug (a *relative*
rpath resolves against the process's cwd, not the `.so`'s own location;
`$ORIGIN` is the token that means the latter), not a guess.

## Built and verified

Same discipline as every other piece of this workspace: built the plugin,
pointed `GST_PLUGIN_PATH` at it, ran `gst-inspect-1.0 neoscrystallize` —
**real registration confirmed**: the correct `GstBaseSink` ancestry, the
`GRAY8`-only pad template, and every property (`tau`, `scale`,
`output-path`, and the four read-only result properties) all show up
exactly as designed. Then a real pipeline:

```bash
gst-launch-1.0 -e videotestsrc num-buffers=20 pattern=ball ! videoconvert ! \
  video/x-raw,format=GRAY8,width=8,height=8,framerate=30/1 ! \
  neoscrystallize tau=3 output-path=result.txt
```

`result.txt` came back with real, non-placeholder crystallisation data —
`nodes: 11`, `input_energy_joules: 2.0173601763e-20`,
`fundamental_hz: 1.5000000000`, `energy_conserving: true` — checked as real
values, the same way `ffi_test`'s printed values were checked against
hand-computable expectations rather than merely "did it run." The error
path was verified too: the same pipeline with `scale=1.0` (opting out of
rescaling) produces a real `GST_ELEMENT_ERROR` naming the exact physical
reason (`signal needs 6.28e47 C_H quanta; only 9.0e15 are exactly
countable`), not a crash or a hang.

## Resolved: the Windows SDK blocker

GStreamer's official Windows development SDK needs administrator
elevation this environment couldn't grant. Resolved via **WSL2/Ubuntu**
(already installed on the host machine) instead of pursuing Windows
elevation: `sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-base
gstreamer1.0-plugins-good libgstreamer1.0-dev
libgstreamer-plugins-base1.0-dev pkg-config build-essential` (run once, by
the repo owner, in an interactive WSL terminal — a normal package install,
not a Windows privilege escalation), then Rust via `rustup` (no sudo
needed). `media_ffi` was confirmed to build and pass its own full test
suite on Linux before any plugin code was written, and the real, existing
C ABI test program (`vendor/heavenos/neos/media_ffi/ffi_test/main.c`) was
extended with a Linux build script and re-verified there too — a second,
independent confirmation (different OS, different compiler) that the FFI
contract holds, not assumed to carry over from the Windows/MSVC one.
