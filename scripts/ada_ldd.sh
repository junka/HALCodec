#!/bin/bash
cd "$(dirname "$0")" || exit 1
echo "--- hal_dec deps"
ldd ./hal_dec | grep -iE "cuda|cuvid|nvenc|halcodec|not found"
echo "--- libnvenc_layers.so deps"
ldd ./libnvenc_layers.so | grep -iE "cuda|cuvid|nvenc|halcodec|not found"
echo "--- which libs are on LD_LIBRARY_PATH dir"
ls -l libcuda* libnvcuvid* libnvidia-encode* libcudart* 2>&1
