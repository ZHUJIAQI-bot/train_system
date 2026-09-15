# 网页版（WebAssembly）

把仓库根目录的 `train_model.c` **原样**编译成 WebAssembly，在浏览器里跑同一套 C 逻辑。
界面是重新写的 HTML/CSS/JS，因此不再受 Windows 字体路径和 raylib 的限制。

- 业务逻辑（校验、座位分配、统计、存档）**全部在 C 里**，网页版只是它的第三个前端
- 与桌面版共用 `train_model.c`，模型改动会同时影响两端
- 存档格式是逐字段小端定长，**浏览器导出的存档可以直接喂给桌面版**，反之亦然

## 构建

需要 [emsdk](https://github.com/emscripten-core/emsdk)（约 1GB）。

### 首次安装

```bash
git clone https://github.com/emscripten-core/emsdk.git D:/emsdk
cd /d/emsdk
"<Python 可执行文件的绝对路径>" emsdk.py install 3.1.64
"<Python 可执行文件的绝对路径>" emsdk.py activate 3.1.64
```

> **Windows 上最大的坑**：`python` 这个命令是「应用商店」的存根，直接跑 `emsdk.bat` 会失败，
> 而报错信息具有误导性。必须用真解释器的绝对路径，例如：
>
> ```
> "C:\Users\<你>\AppData\Local\Programs\Python\Python314\python.exe" emsdk.py install 3.1.64
> ```
>
> 用 `py -0p` 可以列出机器上所有已安装的 Python 及其路径。
> 顺带一提，emsdk 会自己下载一份便携 Python 和 Node，所以系统没装 Node 也不影响构建。

### 构建网页版

```bash
./web/build.sh              # 发布构建
DEBUG_BUILD=1 ./web/build.sh   # 开发构建（-O0 + 断言 + source map）
```

Windows cmd 下用 `web\build.bat`。找不到 emcc 时脚本会自动尝试激活 `D:\emsdk`，
也可以用 `EMSDK_DIR` 指定别处。

产物在 `web/dist/`（已被 `web/.gitignore` 排除，不入库）。

## 本地运行

wasm 需要 http 协议，**建议起一个本地服务器**：

```bash
cd web/dist && py -m http.server 8000
# 然后打开 http://localhost:8000
```

构建时带了 `-sSINGLE_FILE=1`（wasm 以 base64 内嵌）和 `-sEXPORT_ES6=0`（经典 script，
不是 ES module），所以**双击 `web/dist/index.html` 也能打开**。但主路径仍推荐 http：

- `file://` 下 Safari 会拒绝 localStorage
- Chrome 里所有 `file://` 页面共享同一个存储域，调试时的假数据会和正式数据混在一起

## 测试

```bash
node web/test/smoke.mjs
```

在 Node 里无头跑同一份 wasm，覆盖 JSON 转义、姓名字节边界、区段复用、车厢人数平均、
存档往返、以及**损坏存档拒收**——最后这条是开发期用干净小数据完全测不出来的路径。

## 部署到 GitHub Pages

推送到 `main` 后由 `.github/workflows/pages.yml` 自动构建并发布，地址形如
`https://<用户名>.github.io/train_system/`。

**需要在仓库里手动做一次**（自动化代劳不了）：

1. **Settings → Pages → Build and deployment → Source 选 "GitHub Actions"**
   （不是 "Deploy from a branch"）
2. **仓库必须是 public** —— 私有仓库启用 Pages 需要付费计划
3. 账号需已完成邮箱验证

工作流里有一道原生 `ctest` 闸门：模型层被改坏时不会让它上线。

## 存档互通

网页版用浏览器 localStorage，桌面版用 `passengers.dat`，是**两份独立的数据**，
演示时不要误当成 bug。但两者的存档可以互相搬运：

- 网页版「导出存档」得到的 `.dat` → 放到桌面版能读到的地方
- 桌面版的 `passengers.dat` → 用网页版「导入存档」载入

这也顺便解决了 localStorage 不可用（隐私模式、企业策略）时的兜底问题。

## 实现上的几个关键决定

**日期一律由 JS 按本地时区计算，不从 C 取。**
`today_string()` / `date_offset_string()` 依赖 `localtime`/`mktime`，在 Emscripten 下是 JS host
function，跨时区和系统时钟异常时不可靠。另外 JS 侧**绝不能用 `toISOString().slice(0,10)`**——
那给的是 UTC 日期，UTC+8 用户在凌晨 0–8 点会拿到「昨天」，导致今天买的票被当成过期。

**存档被拒收时进入只读熔断态。**
`load_passengers` 校验失败时会把文件改名备份，此时 MEMFS 里已无存档。如果不加熔断，
后续任何一次「改动后保存」都会写出**一份空存档覆盖用户的 localStorage**，
而备份留在 MEMFS 里随刷新蒸发。所以拒收后一律禁止写回，只允许导出。

**过期票清理有三道锁。**
`remove_expired_passengers` 会按日期全量删除，而浏览器里唯一副本就是 localStorage。
因此清理前会检查年份范围、检测时钟倒流（今天的日期早于任何一张票的出行日期），
并把清理前的存档备份到 `train.v1.beforeExpire`。

**渲染一律用 `textContent`，不用 `innerHTML` 塞旅客数据。**
否则存档里的 `<img onerror=...>` 就是现成的存储型 XSS。

**base64 必须分块。**
`String.fromCharCode.apply(null, u8)` 在 100KB 以上会
`RangeError: Maximum call stack size exceeded`。

**`<select>` 的空值要显式处理。**
`parseInt('')` 得到 `NaN`，经 cwrap 的 `'number'` 传进 C 会变成 `0`，
而 0 既是合法的「上海」也是合法的「二等座」——空选择会被静默当成一笔合法订单。

## 文件说明

```
web/
  build.sh / build.bat   构建脚本，与 CI 共用同一套 emcc 参数
  src/web_api.c          C↔JS 胶水层（唯一 include <emscripten.h> 的文件）
  public/                界面（构建时原样拷进 dist/）
  test/smoke.mjs         Node 无头测试
  .gitignore             dist/ 不入库（根 .gitignore 只覆盖 build*/）
```

> **编码提醒**：所有源文件都是 UTF-8 无 BOM。若在中文 Windows 上用 GBK 保存
> `web_api.c`，车站名会全变乱码。
