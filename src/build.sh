#!/usr/bin/env bash
# Builds the neoscrystallize GStreamer plugin, linking against the
# already-built media_ffi shared library. Run `cargo build -p media-ffi`
# in vendor/heavenos/neos first.
set -euo pipefail
cd "$(dirname "$0")"

MEDIA_FFI_DIR="../vendor/heavenos/neos/media_ffi"
MEDIA_FFI_LIB="../vendor/heavenos/neos/target/debug"

gcc -shared -fPIC -Wall \
    -o libgstneoscrystallize.so \
    gstneoscrystallize.c \
    $(pkg-config --cflags --libs gstreamer-1.0 gstreamer-base-1.0 gstreamer-video-1.0) \
    -I"$MEDIA_FFI_DIR" \
    -L"$MEDIA_FFI_LIB" \
    -lmedia_ffi \
    -Wl,-rpath,'$ORIGIN/'"$MEDIA_FFI_LIB"

echo "built: $(pwd)/libgstneoscrystallize.so"
