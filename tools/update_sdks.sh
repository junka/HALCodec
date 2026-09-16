#!/usr/bin/env bash
# update_sdks.sh — 下载 / 更新本项目使用的硬件编解码 SDK。
#
#   用法:
#      tools/update_sdks.sh              # 更新全部 SDK（本地已是最新版本的跳过）
#      tools/update_sdks.sh amf qsv vplgpu # 只更新指定 SDK (nvdec|amf|qsv|vplgpu)
#      tools/update_sdks.sh -v 2.16.0 qsv      # 指定 release tag（默认 latest）
#      tools/update_sdks.sh --check      # 只对比本地版本与上游 latest，不下载
#      tools/update_sdks.sh --force amf  # 本地已存在也重新下载覆盖
#      tools/update_sdks.sh --prune amf  # 额外清理 .gitignore 排除的大目录(如 AMF Thirdparty)
#      tools/update_sdks.sh --prune-old  # 清理同一 SDK 前缀下的旧版本目录
#      tools/update_sdks.sh --import video_codec_sdk_13.1.15.zip nvdec
#                                    # 离线导入本地已下载的包(官网登录下载的场景)
#
#   下载优先顺序:
#     1) GitHub 对应仓库 latest/latest-release 的发布资产 (release assets)
#     2) 无匹配资产时回退到该 release tag 的源码包 (codeload tarball)
#     3) nvdec 无公开下载源, 提示官网下载后走 --import 导入
#   网络受限时用 GITHUB_TOKEN 环境变量提升 GitHub API 配额。
#   GitHub 代理: curl 不遵循 git 的 url.*.insteadOf 重写规则, 脚本会显式读取
#   该规则 (如 gh-proxy.org) 并套用到 github.com 下载 URL; 也可用环境变量
#   GITHUB_PROXY 强制指定前缀式代理 (如 GITHUB_PROXY=https://gh-proxy.org/)。
set -euo pipefail

# 仓库根 = 脚本所在目录的上一级 (脚本位于 tools/ 下)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# SDK 配置: name\trepo(owner/repo)\t资产匹配正则\t目标基目录\t目标目录前缀\t模式
#   mode = auto   : 走 GitHub release 流水线
#   mode = manual : 无公开下载源, 需官网下载后 --import 导入
SDKS=(
  $'nvdec\tNVIDIA/video-codec-sdk\t\.zip\tNVEnc\tVideo_Codec_SDK_\tmanual'
  $'amf\tGPUOpen-LibrariesAndSDKs/AMF\t\.zip\tAMF\tAMF-\tauto'
  $'qsv\tintel/libvpl\t\.(tar\.gz|tgz)\tQSV\tlibvpl-\tauto'
  $'vplgpu\tIntel/vpl-gpu-rt\t\.tar\.gz\tQSV\tvpl-gpu-rt-\tauto'
)

# 统一浏览器 UA；GitHub HTML/重定向对 curl 默认 UA 较敏感
UA="Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 Chrome/120 Safari/537.36"

# GitHub 前缀式代理: curl 不读 git 的 url.*.insteadOf 配置, 这里手动对齐 —
# 优先级: 环境变量 GITHUB_PROXY > git config 中匹配 github.com 的 insteadOf 规则 > 无代理。
# 返回形如 "https://gh-proxy.org/" 的前缀(可能为空), wrap_github() 负责拼接。
detect_github_proxy() {
  if [[ -n "${GITHUB_PROXY:-}" ]]; then
    printf '%s\n' "${GITHUB_PROXY%/}/"
    return
  fi
  local rule
  rule="$(git config --global --get-regexp '^url\..*\.insteadof$' 2>/dev/null | while read -r key val; do
    # key 形如 url.https://gh-proxy.org/https://github.com/.insteadof,
    # 提取其中的 github.com 前缀, 与 val 比对得到代理前缀
    if [[ "$key" == *github.com* ]]; then
      local proxy="${key#url.}"; proxy="${proxy%.insteadof}"
      proxy="${proxy%"https://github.com/"}"
      if [[ "$val" == "https://github.com/" ]]; then
        printf '%s\n' "${proxy%/}/"
        break
      fi
    fi
  done)"
  printf '%s\n' "$rule"
}

GITHUB_PROXY_PREFIX="$(detect_github_proxy)"
if [[ -n "$GITHUB_PROXY_PREFIX" ]]; then
  echo "github proxy: $GITHUB_PROXY_PREFIX (from git insteadOf / GITHUB_PROXY)"
fi

wrap_github() { # $1 = https://github.com/... URL -> 套用代理前缀
  printf '%s\n' "${GITHUB_PROXY_PREFIX}$1"
}

CHECK=0
FORCE=0
PRUNE=0
PRUNE_OLD=0
TAG=""
IMPORT_ZIP=""
SELECTED=()

usage() {
  sed -n '5,13p' "$0"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check)      CHECK=1 ;;
    -f|--force)   FORCE=1 ;;
    --prune)      PRUNE=1 ;;
    --prune-old)  PRUNE_OLD=1 ;;
    -v|--version) shift; TAG="${1:-}" ;;
    --import)     shift; IMPORT_ZIP="${1:-}" ;;
    -h|--help)    usage; exit 0 ;;
    -*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    *)            SELECTED+=("$1") ;;
  esac
  shift
done

api() { # $1 = API 路径，输出 JSON 到 stdout
  # api.github.com 在本机同样受 SSL/证书问题影响, 一并通过代理路由
  local url
  if [[ -n "$GITHUB_PROXY_PREFIX" ]]; then
    url="${GITHUB_PROXY_PREFIX}https://api.github.com/$1"
  else
    url="https://api.github.com/$1"
  fi
  if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    curl -fsSL -A "$UA" -H "Authorization: token ${GITHUB_TOKEN}" "$url"
  else
    curl -fsSL -A "$UA" "$url"
  fi
}

# 从 release JSON 中取 tag 名
release_tag() { # $1 = JSON 文件
  python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["tag_name"])' "$1"
}

# 从 release JSON 中挑选资产的下载 URL；无匹配或无有效 JSON 输出空
release_asset_url() { # $1 = JSON 文件  $2 = 资产匹配正则
  python3 - "$1" "$2" <<'PYEOF'
import json, re, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception:
    sys.exit(0)  # API 不可用或无效响应 -> 调用方走源码包回退
pat = sys.argv[2]
for a in d.get("assets", []):
    if (re.search(pat, a["name"], re.I)
            and not re.search(r"doc|window|win64|\.deb|patch", a["name"], re.I)):
        print(a["browser_download_url"])
        break
PYEOF
}

# 免 API 兜底: 取仓库最新的版本号 tag（git ls-remote，无需浏览器 UA）
# 接受 "v1.2.3" 与带前缀的 "intel-onevpl-26.3.4" 两种风格, 只要求 tag 以
# 数字版本号结尾。
latest_tag_via_git() { # $1 = owner/repo
  git ls-remote --tags "https://github.com/$1" 2>/dev/null \
    | awk '{sub("refs/tags/", "", $2); if ($2 ~ /[0-9]+(\.[0-9]+)+$/ && $2 !~ /\^\{\}/) print $2}' \
    | python3 -c '
import re, sys
def key(v):
    return [int(x) for x in re.split(r"[._-]", v) if x.isdigit()] or [0]
vs = sys.stdin.read().split()
print(max(vs, key=key) if vs else "")
'
}

# 归一化版本号: v2.17.0 -> 2.17.0; intel-onevpl-26.3.4 -> 26.3.4
# 只取末尾的数字版本号, 丢弃前缀/字母。
normalize_ver() {
  python3 -c '
import re, sys
m = re.search(r"[0-9]+(\.[0-9]+)+", sys.argv[1])
print(m.group(0) if m else sys.argv[1])
' "$1"
}

# 本地已安装的最高版本（按数值语义比较，兼容 macOS 无 sort -V）
local_version() { # $1 = sdk 名
  local name="$1" base="" prefix=""
  IFS=$'\t' read -r _ _ _ base prefix _ <<<"$(printf '%s\n' "${SDKS[@]}" | grep -E "^${name}$(printf '\t')")"
  local versions=()
  for d in "$ROOT/$base"/${prefix}*/; do
    [[ -d "$d" ]] || continue
    versions+=("${d#"$ROOT/$base/$prefix"}")
  done
  if [[ ${#versions[@]} -eq 0 ]]; then
    echo "<none>"
    return
  fi
  python3 -c '
import re, sys
def key(v):
    return [int(x) for x in re.split(r"[._-]", v) if x.isdigit()] or [0]
print(max(sys.argv[1:], key=key).rstrip("/"))
' "${versions[@]}"
}

# 解压资产到目标目录；解压根的目录名与 CMake 约定不符时（如 oneVPL-x → libvpl-x）改名
extract_and_place() { # $1=资产文件  $2=临时工作目录  $3=目标目录
  local ar="$1" work="$2" target="$3"
  mkdir -p "$work"
  case "$ar" in
    *.zip)          unzip -q "$ar" -d "$work" ;;
    *.tar.gz|*.tgz) tar -xzf "$ar" -C "$work" ;;
    *) echo "  unsupported archive: $ar" >&2; return 1 ;;
  esac
  local top="" count=0
  for f in "$work"/*; do
    count=$((count + 1))
    top="$f"
  done
  if [[ "$count" == 1 ]]; then
    # 单顶层条目（嵌套 SDK 目录或单个发布解包），直接以目标名就位
    mv "$top" "$target"
  else
    # 多文件打包（SDK 直接展开在根），合并到目标目录
    mkdir -p "$target"
    mv "$work"/* "$target"
  fi
}

prune_sdk_dir() { # $1 = 目标目录  $2 = sdk 名
  local dir="$1" name="$2"
  case "$name" in
    amf) rm -rf "$dir/Thirdparty" "$dir/.github"; echo "  pruned Thirdparty/.github" ;;
    qsv) rm -rf "$dir/.github"; echo "  pruned .github" ;;
  esac
}

# 就位前处理目标目录的覆盖/清理
prepare_target() { # $1 = 目标目录
  local target="$1"
  if [[ -d "$target" ]]; then
    if [[ "$FORCE" != 1 ]]; then
      echo "  already present, skip (use --force to replace)"
      return 1
    fi
    echo "  replacing existing $target (--force)"
    rm -rf "$target"
  fi
  return 0
}

place_check() { # $1 = 目标目录  $2 = sdk 名
  local target="$1" name="$2"
  if [[ ! -d "$target" ]]; then
    echo "  failed to place SDK at $target" >&2
    return 1
  fi
  if [[ "$PRUNE" == 1 ]]; then
    prune_sdk_dir "$target" "$name"
  fi
  echo "  installed: $target"
}

# 解析上游 (GitHub release 优先) 得到 tag/ver
# 输出 "原始tag<TAB>归一化ver" 到 stdout: 原始 tag 供源码包下载 URL 使用
# (可能带 v/前缀, 如 v2.17.0 或 intel-onevpl-26.2.4), ver 用作目标目录名。
# 注: 不再用全局变量传递 tag, 因为 resolve_upstream 在命令替换子 shell 中
# 调用, 父进程看不到副作用。
resolve_upstream() { # $1=repo  $2=临时目录 -> 输出 "tag\tver" 到 stdout
  local repo="$1" work="$2"
  local json_file="$work/release.json" tag=""
  if [[ -n "$TAG" ]]; then
    api "repos/$repo/releases/tags/$TAG" >"$json_file" 2>/dev/null || {
      echo "release tag not found: $TAG" >&2; return 1; }
    tag="$(release_tag "$json_file")"
  elif api "repos/$repo/releases/latest" >"$json_file" 2>/dev/null; then
    tag="$(release_tag "$json_file")"
  else
    # API 不可用(限流/离线): 免 API 兜底。优先从 /releases/latest 页面
    # 提取 tag; 页面不可解析时回退到 git ls-remote 的最新版本 tag。
    tag="$(curl -fsSL -A "$UA" -L \
           "$(wrap_github "https://github.com/$repo/releases/latest")" 2>/dev/null \
           | grep -oE 'releases/tag/[A-Za-z0-9._-]+' \
           | sed -E 's#releases/tag/##' \
           | grep -E '[0-9]+(\.[0-9]+)+' \
           | head -1)"
    if [[ -z "$tag" ]]; then
      tag="$(latest_tag_via_git "$repo")"
      echo "  (API unavailable, resolved latest tag via git: $tag)" >&2
    else
      echo "  (API unavailable, resolved tag via release page: $tag)" >&2
    fi
    [[ -n "$tag" ]] || { echo "failed to resolve latest release tag" >&2; return 1; }
  fi
  [[ -n "$tag" ]] || { echo "release has no tag" >&2; return 1; }
  printf '%s\t%s\n' "$tag" "$(normalize_ver "$tag")"
}

update_sdk() { # $1 = 配置行
  IFS=$'\t' read -r name repo asset_pat base prefix mode <<<"$1"
  echo "=== $name ($repo) ==="

  if [[ "$mode" == "manual" ]]; then
    echo "  this SDK has no public download source (NVIDIA account required)."
    echo "  download it from: https://developer.nvidia.com/nvidia-video-codec-sdk"
    if [[ "$CHECK" == 1 ]]; then
      echo "  local: $(local_version "$name")"
    else
      echo "  then import the zip with: tools/update_sdks.sh --import <zip> $name"
    fi
    return 0
  fi

  local work
  work="$(mktemp -d)"
  # macOS bash 3.2 在函数返回后才执行 RETURN trap, 此时 local 变量已失效,
  # 因此把路径字面量内插进 trap 命令(路径来自 mktemp, 不含单引号字符)。
  trap "rm -rf '$work'" RETURN

  # ---- 解析上游版本: resolve_upstream 输出 "原始tag\t归一化ver" ----
  local upstream tag ver
  upstream="$(resolve_upstream "$repo" "$work")" || return 1
  IFS=$'\t' read -r tag ver <<<"$upstream"
  echo "  upstream: $ver (tag: $tag)   local: $(local_version "$name")"
  if [[ "$CHECK" == 1 ]]; then
    return 0
  fi

  local target="$ROOT/$base/$prefix$ver"
  prepare_target "$target" || return 0

  if [[ "$PRUNE_OLD" == 1 ]]; then
    for d in "$ROOT/$base"/${prefix}*/; do
      [[ -d "$d" && "$d" != "$target/" ]] && { echo "  removing old: $d"; rm -rf "$d"; }
    done
  fi

  # ---- 下载: release 资产优先, 无则回退源码包 ----
  local url
  url="$(release_asset_url "$work/release.json" "$asset_pat")"
  if [[ -n "$url" ]]; then
    echo "  downloading asset: $url"
  else
    # 源码包 URL 必须用原始 tag (可能带 v 前缀或仓库前缀), 如 v2.17.0
    url="https://github.com/$repo/archive/refs/tags/${tag}.tar.gz"
    echo "  no matching asset, fallback to source tarball: $url"
  fi
  # curl 不走 git insteadOf, 下载前显式套用代理前缀
  url="$(wrap_github "$url")"

  local ar="$work/$(basename "${url%%\?*}")"
  if ! curl -fsSL -A "$UA" -L "$url" -o "$ar"; then
    echo "  download failed: $url" >&2
    return 1
  fi
  echo "  downloaded: $(du -h "$ar" | cut -f1)"

  extract_and_place "$ar" "$work/x" "$target"
  place_check "$target" "$name"
}

import_sdk() { # $1 = 本地压缩包路径  $2 = sdk 名
  local ar="$1" name="$2"
  [[ -f "$ar" ]] || { echo "no such file: $ar" >&2; return 1; }
  IFS=$'\t' read -r _ repo _ base prefix mode <<<"$(printf '%s\n' "${SDKS[@]}" | grep -E "^${name}$(printf '\t')")"
  if [[ -z "$base" || "$mode" != "manual" ]]; then
    echo "sdk '$name' not importable (requires manual mode)" >&2
    available_sdks
    return 1
  fi
  local ver
  if [[ -n "$TAG" ]]; then
    ver="$(normalize_ver "$TAG")"
  else
    ver="$(basename "$ar" | grep -oE '[0-9]+(\.[0-9]+)+' | head -1 || true)"
  fi
  [[ -n "$ver" ]] || { echo "cannot infer version from filename, use -v <ver>" >&2; return 1; }

  local target="$ROOT/$base/$prefix$ver"
  echo "=== import $name -> $target ==="
  prepare_target "$target" || return 0
  local work
  work="$(mktemp -d)"
  trap "rm -rf '$work'" RETURN
  extract_and_place "$ar" "$work/x" "$target"
  place_check "$target" "$name"
}

available_sdks() { echo "available: nvdec|amf|qsv|vplgpu"; }

main() {
  # ---- main ----
  if [[ -n "$IMPORT_ZIP" ]]; then
    if [[ "${#SELECTED[@]}" -ne 1 ]]; then
      echo "--import requires exactly one sdk name (e.g. --import <zip> nvdec)" >&2
      exit 2
    fi
    import_sdk "$IMPORT_ZIP" "${SELECTED[0]}"
    exit $?
  fi

  if [[ "${#SELECTED[@]}" -gt 0 ]]; then
    for s in "${SELECTED[@]}"; do
      line="$(printf '%s\n' "${SDKS[@]}" | grep -E "^${s}$(printf '\t')" || true)"
      if [[ -z "$line" ]]; then
        echo "unknown sdk: $s" >&2
        available_sdks >&2
        continue
      fi
      update_sdk "$line"
    done
  else
    for line in "${SDKS[@]}"; do update_sdk "$line"; done
  fi
  echo "done."
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi