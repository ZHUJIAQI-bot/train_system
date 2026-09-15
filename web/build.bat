@echo off
setlocal enableextensions enabledelayedexpansion

REM 构建网页版（Windows cmd 版）。与 build.sh 使用完全相同的 emcc 参数。
REM 用法：
REM   web\build.bat                 发布构建（-O3）
REM   set DEBUG_BUILD=1 && web\build.bat    开发构建

set "SCRIPT_DIR=%~dp0"
set "ROOT=%SCRIPT_DIR%.."
pushd "%ROOT%"

REM --- 找到 emcc ---------------------------------------------------------------
where emcc >nul 2>&1
if errorlevel 1 (
  if "%EMSDK_DIR%"=="" set "EMSDK_DIR=D:\emsdk"
  if exist "!EMSDK_DIR!\emsdk_env.bat" (
    echo 正在激活 emsdk: !EMSDK_DIR!
    call "!EMSDK_DIR!\emsdk_env.bat"
  )
)

where emcc >nul 2>&1
if errorlevel 1 (
  echo.
  echo 找不到 emcc。请先安装 Emscripten 工具链，或设置 EMSDK_DIR 指向已有安装：
  echo.
  echo   git clone https://github.com/emscripten-core/emsdk.git D:\emsdk
  echo   cd /d D:\emsdk
  echo   "C:\Users\ZJQ\AppData\Local\Programs\Python\Python314\python.exe" emsdk.py install 3.1.64
  echo   "C:\Users\ZJQ\AppData\Local\Programs\Python\Python314\python.exe" emsdk.py activate 3.1.64
  echo.
  echo 注意：Windows 上 python 命令是应用商店的存根，会失败，必须用真解释器的绝对路径。
  popd
  exit /b 1
)

echo emcc: & where emcc

REM --- 静态文件先就位 -----------------------------------------------------------
REM 必须先拷贝，否则 dist\index.html 不存在，发布会「成功」但打开是 404
if not exist "web\dist" mkdir "web\dist"
copy /y "web\public\index.html" "web\dist\" >nul
copy /y "web\public\style.css"  "web\dist\" >nul
copy /y "web\public\app.js"     "web\dist\" >nul

set "OPT=-O3"
if "%DEBUG_BUILD%"=="1" set "OPT=-O0 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=1 -gsource-map"
echo 编译选项: %OPT%

REM --- 编译 ---------------------------------------------------------------------
REM -I. 从仓库根解析头文件（web_api.c 里写的是 #include "train_model.h"）
REM -sEXPORT_ES6=0 + -sSINGLE_FILE=1 才能 file:// 双击打开
emcc web\src\web_api.c train_model.c -I. -std=c17 %OPT% ^
  -o web\dist\train_web.js ^
  --no-entry ^
  -sMODULARIZE=1 -sEXPORT_NAME=createTrainModule -sEXPORT_ES6=0 ^
  -sENVIRONMENT=web,node ^
  -sSINGLE_FILE=1 -sFORCE_FILESYSTEM=1 ^
  -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sMAXIMUM_MEMORY=268435456 ^
  -sSTACK_SIZE=1048576 ^
  -sEXPORTED_FUNCTIONS=_malloc,_free ^
  -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToUTF8,lengthBytesUTF8,cwrap,ccall

if errorlevel 1 (
  echo.
  echo 构建失败。
  popd
  exit /b 1
)

echo.
echo 构建完成 -^> web\dist\
dir /b "web\dist"

popd
endlocal
