#!/usr/bin/env bash
# 构建网页版。本地（Git Bash）与 CI 共用这一份。
#
# 用法：
#   ./web/build.sh                 # 发布构建（-O3）
#   DEBUG_BUILD=1 ./web/build.sh   # 开发构建（-O0 + 断言 + source map）
#
# 环境变量：
#   EMSDK_DIR    emsdk 安装位置，默认 D:/emsdk（本机）或 ../emsdk（CI 检出同级）
#   EMCC_EXTRA_FLAGS  追加给 emcc 的额外参数
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

# --- 找到 emcc -----------------------------------------------------------------
if ! command -v emcc >/dev/null 2>&1; then
  # 本地没把 emsdk 加进 PATH 时，尝试激活一份
  for candidate in "${EMSDK_DIR:-}" "$ROOT/../emsdk" "D:/emsdk" "$HOME/emsdk"; do
    [ -n "$candidate" ] || continue
    if [ -f "$candidate/emsdk_env.sh" ]; then
      # shellcheck disable=SC1090,SC1091
      source "$candidate/emsdk_env.sh" >/dev/null 2>&1 || true
      break
    fi
  done
fi

if ! command -v emcc >/dev/null 2>&1; then
  cat >&2 <<'EOF'
找不到 emcc。

请先安装 Emscripten 工具链（约 1GB），或设置 EMSDK_DIR 指向已有安装：

  git clone https://github.com/emscripten-core/emsdk.git D:/emsdk
  cd D:/emsdk
  "<真 Python 的绝对路径>" emsdk.py install 3.1.64
  "<真 Python 的绝对路径>" emsdk.py activate 3.1.64

注意 Windows 上 `python` 是应用商店的存根，会失败，必须用真解释器路径。
EOF
  exit 1
fi

echo "emcc: $(command -v emcc)"
emcc --version | head -1

# --- 静态文件先就位 -------------------------------------------------------------
# 必须先拷贝：否则 dist/index.html 不存在，Pages 发布会「成功」但打开是 404
mkdir -p web/dist
cp web/public/index.html web/public/style.css web/public/app.js web/dist/

if [ "${DEBUG_BUILD:-0}" = "1" ]; then
  OPT_FLAGS="-O0 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=1 -gsource-map"
  echo "模式: 开发构建"
else
  OPT_FLAGS="-O3"
  echo "模式: 发布构建"
fi

# --- 编译 ----------------------------------------------------------------------
# -I. 从仓库根解析头文件。web_api.c 里若写 "../train_model.h" 会解析到
#     不存在的 web/train_model.h，务必用 -I. 配 #include "train_model.h"。
#
# -sEXPORT_ES6=0 + -sSINGLE_FILE=1 是「双击打开」的关键：
#   wasm 以 base64 内嵌，走 WebAssembly.instantiate(ArrayBuffer) 不碰 fetch，
#   因此不受 file:// 的 opaque-origin 限制；而 ES module 在 file:// 下会被 CORS 挡死。
#
# 只导出 _malloc/_free：api_* 都用 EMSCRIPTEN_KEEPALIVE 标注，
# KEEPALIVE 会自动追加到导出表，省得逐个维护函数名。
# shellcheck disable=SC2086
emcc web/src/web_api.c train_model.c -I. \
     -std=c17 $OPT_FLAGS ${EMCC_EXTRA_FLAGS:-} \
     -o web/dist/train_web.js \
     --no-entry \
     -sMODULARIZE=1 -sEXPORT_NAME=createTrainModule -sEXPORT_ES6=0 \
     -sENVIRONMENT=web,node \
     -sSINGLE_FILE=1 -sFORCE_FILESYSTEM=1 \
     -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=268435456 \
     -sSTACK_SIZE=1048576 \
     -sEXPORTED_FUNCTIONS=_malloc,_free \
     -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToUTF8,lengthBytesUTF8,cwrap,ccall

echo
echo "构建完成 → web/dist/"
ls -la web/dist/
