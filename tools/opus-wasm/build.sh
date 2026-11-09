#!/bin/bash
# Build libopus thanh file opus.wasm doc lap (khong can glue Emscripten) cho may chu AI tren Cloudflare.
set -e
V=1.5.2
curl -fsSL https://downloads.xiph.org/releases/opus/opus-$V.tar.gz | tar xz
cd opus-$V
emconfigure ./configure --disable-shared --enable-static --disable-doc --disable-extra-programs --disable-rtcd --disable-intrinsics --disable-stack-protector --enable-fixed-point=no CFLAGS="-O3"
emmake make -j4
emcc -O3 .libs/libopus.a -o ../opus.wasm --no-entry \
  -s STANDALONE_WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=4MB -s FILESYSTEM=0 \
  -s EXPORTED_FUNCTIONS='["_opus_encoder_create","_opus_encode","_opus_encoder_destroy","_opus_encoder_ctl","_malloc","_free"]'
cd .. && ls -la opus.wasm
