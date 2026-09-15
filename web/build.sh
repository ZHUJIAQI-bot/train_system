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

# --- 定位 emsdk 并准备环境 -------------------------------------------------------
# 不直接 source emsdk_env.sh：那个脚本内部也会调用 `python`，
# 在 Windows 上会命中应用商店的存根而失败。这里手动设置它本来要设的几个变量。
EMSDK_DIR="${EMSDK_DIR:-}"
if [ -z "$EMSDK_DIR" ]; then
  for candidate in "D:/emsdk" "$ROOT/../emsdk" "$HOME/emsdk"; do
    if [ -f "$candidate/emsdk.py" ]; then EMSDK_DIR="$candidate"; break; fi
  done
fi

if [ -n "$EMSDK_DIR" ] && [ -f "$EMSDK_DIR/.emscripten" ]; then
  # .emscripten 里写着 emsdk_path = os.path.dirname(os.getenv('EM_CONFIG'))，
  # 所以 EM_CONFIG 必须显式给出，否则解析不出各工具的路径
  export EM_CONFIG="${EM_CONFIG:-$EMSDK_DIR/.emscripten}"

  # PATH 必须用 POSIX 风格：Git Bash 只认 /d/... 不认 D:/...，
  # 直接塞 D:/emsdk/... 进去的话 command -v 永远找不到，而且不报任何错。
  EMSDK_POSIX="$EMSDK_DIR"
  if command -v cygpath >/dev/null 2>&1; then
    EMSDK_POSIX="$(cygpath -u "$EMSDK_DIR")"
  fi
  export PATH="$EMSDK_POSIX/upstream/emscripten:$PATH"

  # emcc 是个 #!/usr/bin/env python 的脚本，需要一个能用的 python 才能启动。
  # 用 emsdk 自带的那个，不依赖系统是否装了 Python。
  if [ -z "${EMSDK_PYTHON:-}" ]; then
    for py in "$EMSDK_DIR"/python/*/python.exe "$EMSDK_DIR"/python/*/bin/python3; do
      if [ -x "$py" ]; then export EMSDK_PYTHON="$py"; break; fi
    done
  fi
fi

# --- 找到可用的 emcc -------------------------------------------------------------
# 必须实际跑一次 --version 来判定，不能只看 command -v：
# Windows 上 PATH 里的 `emcc`（无扩展名）是个 #!/usr/bin/env python 脚本，
# 它能被 command -v 找到，但一执行就命中应用商店的 python 存根而失败，
# 而且它不认 EMSDK_PYTHON（只有 .bat 版本认）。所以 .bat 优先。
# 在 Linux/CI 上 emcc.bat 不存在，command -v 会失败，自然回落到 emcc。
EMCC=""
for candidate in emcc.bat emcc; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" --version >/dev/null 2>&1; then
    EMCC="$candidate"
    break
  fi
done

if [ -z "$EMCC" ]; then
  cat >&2 <<'EOF'
找不到可用的 emcc。

请先安装 Emscripten 工具链（约 1GB），或设置 EMSDK_DIR 指向已有安装：

  git clone https://github.com/emscripten-core/emsdk.git D:/emsdk
  cd D:/emsdk
  "<真 Python 的绝对路径>" emsdk.py install 3.1.64
  "<真 Python 的绝对路径>" emsdk.py activate 3.1.64

Windows 上的两个坑（详见 README）：
  1. `python` 是应用商店的存根，必须给真解释器的绝对路径
  2. emsdk 安装失败时退出码仍是 0，要用 emcc.bat 是否存在来判定成功
EOF
  exit 1
fi

echo "emsdk: $EMSDK_DIR"
echo "emcc:  $(command -v "$EMCC")"
"$EMCC" --version | head -1

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
"$EMCC" web/src/web_api.c train_model.c -I. \
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
