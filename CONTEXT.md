# Media Framework for HeavenOS — the `neoscrystallize` GStreamer element

This is the design for the plugin this repo exists to build, written so
implementation can start immediately once the GStreamer development SDK is
actually available in the build environment — see the "Blocked" note at
the bottom. Nothing below is speculative about what `media_ffi` provides;
every signature referenced here is a real, already-verified function in
`vendor/heavenos/neos/media_ffi` (opaque handle, panic-guarded, ownership
correct, proven against a real independent C program).

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

Pixel conversion is direct, not normalised: `media_ffi`'s own frames are
each GRAY8 byte cast straight to `f64` (`byte as f64`, range 0-255) —
confirmed against `crystallisation::codec::decode_ppm`'s own real
behaviour (`body[off] as f64`, no rescaling), not assumed.

## Properties

| Name | Type | Direction | Purpose |
|---|---|---|---|
| `tau` | `uint`, default `3` | read/write | Takens embedding delay, passed straight to `media_ffi_crystallise_video`. |
| `output-path` | `string`, optional | read/write | If set, write a plain-text summary here when crystallisation completes. |
| `node-count` | `uint64` | read-only | Set after EOS from `media_ffi_video_result_node_count`. |
| `input-energy` | `double` | read-only | Set after EOS from `media_ffi_video_result_input_energy`. |
| `fundamental-hz` | `double` | read-only | Set after EOS from `media_ffi_video_result_fundamental_hz`. |
| `energy-conserving` | `boolean` | read-only | Set after EOS from `media_ffi_video_result_is_energy_conserving`. |

## Internal state

- A growing `Vec<u8>` (or a C `uint8_t` buffer, depending on implementation
  language — see below) accumulating every frame's raw pixel bytes, in
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

C, against `gstreamer-1.0`/`gstbase-1.0`'s own headers, linking directly
against `media_ffi`'s built `.dll`/`.lib` the same way
`vendor/heavenos/neos/media_ffi/ffi_test/main.c` already does — no new
language or binding layer needed, since `media_ffi.h` already exists and
is already proven correct against a real MSVC-compiled C program.

## Verification plan, once buildable

Same discipline as every other piece of this workspace: build the plugin,
point `GST_PLUGIN_PATH` at it, run `gst-inspect-1.0 neoscrystallize` to
confirm real registration (not just that it compiled), then a real
pipeline —

```
gst-launch-1.0 videotestsrc num-buffers=20 ! videoconvert ! \
  video/x-raw,format=GRAY8 ! neoscrystallize tau=3 output-path=result.txt
```

— confirming `result.txt` contains real, non-placeholder crystallisation
data, the same way `ffi_test.exe`'s printed values were checked against
hand-computable expectations rather than merely "did it run."

## Blocked

GStreamer's official Windows development SDK
(`gstreamer-1.0-msvc-x86_64-1.28.6.exe`) requires administrator
elevation to install, which the environment this design was written in
cannot grant — no interactive session exists to approve the UAC prompt,
and `winget install` reports success without anything actually landing on
disk (confirmed by searching for it afterward: no directory, no registry
key, no running process). Not yet resolved. `vcpkg` (no admin required,
but may build GStreamer from source rather than fetch a binary) is the
untried next option.
