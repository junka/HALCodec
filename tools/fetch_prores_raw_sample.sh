#!/usr/bin/env bash
# fetch_prores_raw_sample.sh — 取一份 ProRes RAW ('prrf') 元素码流给
# ctest 的 vtdec.prores_raw / proresdesc 用。
#
#   用法:
#     tools/fetch_prores_raw_sample.sh                 # 拉到 tests/data/，2 帧
#     tools/fetch_prores_raw_sample.sh --frames 3 --out /tmp/samples
#     tools/fetch_prores_raw_sample.sh --list          # 只打印帧表，不写文件
#     tools/fetch_prores_raw_sample.sh --from window.bin  # 用已存下来的开头窗口，不联网
#     tools/fetch_prores_raw_sample.sh golden <sample.prrf> <native-b16q-planes.bin>
#
#   为什么需要脚本而不是把素材放进仓库：
#   ProRes RAW 没有编码器 —— VideoToolbox 的 VTCopyVideoEncoderList 里只有
#   422/4444 六档，ffmpeg 的 prores_raw 解码器也没有对应编码器（实测 D.VIL.）。
#   所以这条硬件路径的唯一码流来源是第三方素材，而第三方 RAW 素材的许可由
#   维护者决定，不应默认入库。本脚本按需从公开样本站取，产物落在
#   .gitignore 排除的 tests/data/prores_raw_*.prrf。
#
#   素材来源（FFmpeg 官方样本站，ticket7887 = ProRes RAW 解码 issue）：
#     https://samples.ffmpeg.org/ffmpeg-bugs/trac/ticket7887/
#     Filmplusgear-ProRes-RAW-testfiles-6.mov, 1041311744 B (FILMplusgear 的
#     公开测试片，4112x2176 —— 即 8K 传感器的 16:9 开窗)。
#   整个文件 1 GB，但 ProRes 帧自带 32-bit 大端长度前缀，是连续可定界的，
#   所以只用 range 请求取开头 12 MiB 就能切出前 2 帧，不需要 moov。
#   （取回的 stsz 与帧头长度精确一致，是 #42 探针里核对过的。）
#
#   golden 那一支做什么：把 VideoToolbox **原生** 'b16q' 四平面输出（每平面
#   H/2 x W/2 的 16-bit sensel）按奇偶重排成整幅网格，取前 8 行，写成
#   <sample>.golden8.bin。它是内容对拍用的，与被测代码无关 —— 原生四平面
#   输出必须由一个不指定 destination pixel format 的解码器给出（HAL 解 RAW
#   时固定请求 'bp16'，永远不会自己产出这个布局）。**不要用 hal_dec 的输出
#   当 golden**，那等于让被测代码给自己出标准答案。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE_URL="https://samples.ffmpeg.org/ffmpeg-bugs/trac/ticket7887/Filmplusgear-ProRes-RAW-testfiles-6.mov"

OUT_DIR="${ROOT}/tests/data"
FRAMES=2
WINDOW=$((12 * 1024 * 1024))   # 每帧 ~5.87 MB，12 MiB 覆盖前 2 帧
CHUNK=$((4 * 1024 * 1024))
LIST_ONLY=0
FROM=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --frames) FRAMES="$2"; shift 2 ;;
    --out)    OUT_DIR="$2"; shift 2 ;;
    --window) WINDOW="$2"; shift 2 ;;
    --from)   FROM="$2"; shift 2 ;;
    --list)   LIST_ONLY=1; shift ;;
    -h|--help) sed -n '2,40p' "${BASH_SOURCE[0]}"; exit 0 ;;
    golden)
      # golden <sample.prrf> <native four-plane dump>
      if [[ $# -ne 3 ]]; then
        echo "usage: $0 golden <sample.prrf> <native-b16q-planes.bin>" >&2
        exit 2
      fi
      python3 - "$2" "$3" <<'PY'
import sys
try:
    import numpy as np
except ImportError:
    sys.exit("golden 需要 numpy（python3 -m pip install numpy）")

sample, native = sys.argv[1], sys.argv[2]
data = open(sample, "rb").read()

# 帧头：u32 长度 + 'prrf' + RAW 的 0x0088（422/4444 才是 0x0094）+ producer，
# 然后 be16 宽@16 / be16 高@18。
import struct
ln = struct.unpack_from(">I", data, 0)[0]
w, h = struct.unpack_from(">HH", data, 16)
assert data[4:8] == b"prrf", f"not a prores raw stream: tag={data[4:8]!r}"
assert ln >= 20 and ln <= len(data), f"frame 0 declares {ln} B, file is {len(data)} B"
pw, ph = w // 2, h // 2
plane = ph * pw * 2
raw = open(native, "rb").read()
if len(raw) != 4 * plane:
    sys.exit(f"native dump must be exactly 4 planes of {pw}x{ph} 16-bit "
             f"({4 * plane} B), got {len(raw)} B -- 说明它不是同一尺寸的"
             f" 'b16q' 输出")
p = [np.frombuffer(raw[i * plane:(i + 1) * plane], dtype="<u2").reshape(ph, pw)
     for i in range(4)]
grid = np.empty((h, w), dtype="<u2")
grid[0::2, 0::2] = p[0]   # (even x, even y)
grid[0::2, 1::2] = p[1]   # (odd  x, even y)
grid[1::2, 0::2] = p[2]   # (even x, odd  y)
grid[1::2, 1::2] = p[3]   # (odd  x, odd  y)
out = f"{sample}.golden8.bin"
# 只存前 8 行：足够钉住"平面顺序 = 相位顺序"这件事，又不至于把整帧第三方
# sensel 复制一份进测试目录。
open(out, "wb").write(grid[:8].tobytes())
print(f"wrote {out}: {8} rows x {w} sensels of frame 0 "
      f"({w * 2 * 8} B), re-interleaved from {native}")
PY
      exit 0 ;;
    *) echo "unknown argument: $1 (try --help)" >&2; exit 2 ;;
  esac
done

TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

if [[ -n "$FROM" ]]; then
  echo "==> reusing cached window ${FROM} ($(($(stat -f%z "$FROM") / 1024 / 1024)) MiB), no network"
  WINDOW="$(stat -f%z "$FROM")"
  WIN_FILE="$FROM"
else
  echo "==> range-fetch $((WINDOW / 1024 / 1024)) MiB from samples.ffmpeg.org"
  # 这个站实测只有 ~5-10 KB/s 且会整段停住（#42 拉 4 MiB 的 tail 重试了 8 轮），
  # 所以每块单独落盘并用 -C 续传：--speed-limit/--speed-time 把卡死的连接掐掉，
  # 下一轮从该块已写下的字节数接着要，不从头再来。
  WIN_FILE="${TMP}/window.bin"
  : > "${WIN_FILE}"
  for ((off = 0; off < WINDOW; off += CHUNK)); do
    end=$((off + CHUNK - 1))
    if ((end >= WINDOW)); then end=$((WINDOW - 1)); fi
    want=$((end - off + 1))
    part="${TMP}/part.bin"
    for ((try = 1; try <= 12; try++)); do
      have=0
      [[ -f "${part}" ]] && have="$(stat -f%z "${part}")"
      if ((have >= want)); then break; fi
      echo "    ${off}-${end} (have ${have}/${want}, attempt ${try})"
      # -C - 让 curl 自己按 -o 文件的现有大小续传（会把 Range 起点抬到 off+N）。
      # 不要写成 -C "-$have"：负数形式的参数 curl 直接拒绝（"expected a positive
      # numerical parameter"），have=0 时整轮都跑不起来。用 -o 而不是 >>，避免
      # 站点忽略 Range 时把正文重复追加进来。
      curl -sS --fail --speed-limit 4096 --speed-time 30 --max-time 900 \
           -r "${off}-${end}" -C - "${BASE_URL}" -o "${part}" || true
    done
    have="$(stat -f%z "${part}" 2>/dev/null || echo 0)"
    if ((have < want)); then
      echo "停在 ${off}-${end}（只拿到 ${have}/${want} 字节）—— samples.ffmpeg.org" >&2
      echo "此刻不可用。换网络重试，或自己存好开头窗口后用 --from <file> 走本地。" >&2
      exit 1
    fi
    cat "${part}" >> "${WIN_FILE}"
    rm -f "${part}"
  done
fi

# 切帧
python3 - "$WIN_FILE" "$WINDOW" "$FRAMES" "$OUT_DIR" "$LIST_ONLY" <<'PY'
import os, struct, sys, hashlib

win_file, window, want, out_dir, list_only = sys.argv[1], int(sys.argv[2]), \
    int(sys.argv[3]), sys.argv[4], sys.argv[5] == "1"

d = open(win_file, "rb").read()
print(f"contiguous window: {len(d)} bytes")
if len(d) < window:
    sys.exit(f"short window: wanted {window}, got {len(d)} -- 重试或加大 --from 的文件")

# 候选帧起点：'prrf' 出现在 i+4，且 i 处的 u32 是它自己的长度。
cands = []
i = 0
while True:
    j = d.find(b"prrf", i)
    if j < 0:
        break
    start = j - 4
    if start >= 0:
        ln = struct.unpack_from(">I", d, start)[0]
        if ln >= 20 and start + ln <= len(d):
            cands.append((start, ln))
    i = j + 1

# 沿长度前缀往后接龙，取能连出最多帧的那个起点。
best = []
for start, _ in cands:
    chain, off = [], start
    while True:
        k = next((c for c in cands if c[0] == off), None)
        if not k:
            break
        chain.append(k)
        off += k[1]
    if len(chain) > len(best):
        best = chain
if not best:
    sys.exit("no self-delimiting 'prrf' frames found -- 样本站的文件结构变了，"
             "先手工核对帧头偏移再改这个脚本")

print(f"frame chain found at offset {best[0][0]}: {len(best)} frame(s)")
for idx, (off, ln) in enumerate(best):
    tag = d[off + 4:off + 8]
    w, h = struct.unpack_from(">HH", d, off + 16)
    print(f"  frame{idx} @{off} len={ln} tag={tag.decode()} {w}x{h} "
          f"const@8=0x{struct.unpack_from('>H', d, off + 8)[0]:04x}")

if list_only:
    sys.exit(0)
if len(best) < want:
    sys.exit(f"only {len(best)} complete frames in a {window // (1024*1024)} MiB "
             f"window, asked for {want} -- 加大 --window")
best = best[:want]

w, h = struct.unpack_from(">HH", d, best[0][0] + 16)
body = b"".join(d[off:off + ln] for off, ln in best)
assert len(body) == sum(ln for _, ln in best)
os.makedirs(out_dir, exist_ok=True)
out = os.path.join(out_dir, f"prores_raw_{w}x{h}_{len(best)}f.prrf")
open(out, "wb").write(body)
print(f"wrote {out}: {len(body)} B = "
      f"{'+'.join(str(ln) for _, ln in best)}")
print(f"sha256 {hashlib.sha256(body).hexdigest()}")
print()
print("enable the case with either of:")
print(f"  cmake -S . -B build -DHAL_PRORES_RAW_SAMPLE={out}")
print("  (tests/CMakeLists.txt also globs data/prores_raw_*.prrf, so a plain")
print("   reconfigure picks it up automatically)")
PY
