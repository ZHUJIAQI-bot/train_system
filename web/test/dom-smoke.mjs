/* ============================================================
   界面层无头测试 —— 在 jsdom 里加载真实页面，验证 app.js 的 DOM 代码。

   与 smoke.mjs 的分工：那个测 wasm 侧的业务逻辑，这个测前端接线
   （元素 id 是否对得上、表单提交流程、退票、标签页、语言切换、i18n 渲染）。

   需要 jsdom：npm install jsdom
   未安装时自动跳过（不算失败），因为它是可选依赖。
   ============================================================ */
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import fs from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
const here = path.dirname(fileURLToPath(import.meta.url));
const distDir = path.join(here, '..', 'dist');

let JSDOM;
try {
  ({ JSDOM } = require('jsdom'));
} catch {
  console.log('跳过 DOM 测试：未安装 jsdom（npm install jsdom 后可运行）。');
  process.exit(0);
}

if (!fs.existsSync(path.join(distDir, 'index.html'))) {
  console.error('找不到 web/dist/index.html —— 请先运行 web/build.sh 或 web/build.bat');
  process.exit(2);
}

let checks = 0;
let failures = 0;
const check = (cond, what) => {
  checks++;
  if (!cond) { failures++; console.log('  FAIL: ' + what); }
};

const html = fs.readFileSync(path.join(distDir, 'index.html'), 'utf8');
const dom = new JSDOM(html, {
  url: 'http://localhost/',
  runScripts: 'dangerously',
  pretendToBeVisual: true
});
const { window } = dom;

const errors = [];
window.addEventListener('error', (e) => errors.push(e.error ? e.error.stack : e.message));
window.addEventListener('unhandledrejection', (e) => errors.push('unhandledrejection: ' + e.reason));

// 手动注入脚本内容，避免 jsdom 去走网络
function inject(file) {
  const code = fs.readFileSync(path.join(distDir, file), 'utf8');
  const el = window.document.createElement('script');
  el.textContent = code;
  window.document.body.appendChild(el);
}

inject('train_web.js');
inject('app.js');

const wait = (ms) => new Promise((r) => setTimeout(r, ms));
await wait(4000);

const doc = window.document;
const $ = (id) => doc.getElementById(id);
const fire = (el, type) => el.dispatchEvent(new window.Event(type, { bubbles: true, cancelable: true }));

console.log('DOM 接线');
check(errors.length === 0, '页面不应有未捕获异常：' + errors.join(' | '));
check($('f-date').options.length === 4, '日期下拉应有 4 项（实际 ' + $('f-date').options.length + '）');
check($('f-train').options.length === 10, '车次下拉应有 10 项（实际 ' + $('f-train').options.length + '）');
check($('f-board').options.length === 6, '上车站下拉应有 6 项');
check($('f-alight').options.length === 6, '下车站下拉应有 6 项');
check($('f-class').options.length === 2, '等级下拉应有 2 项');

const trainLabel = $('f-train').options[0].textContent;
check(/G1/.test(trainLabel) && /06:00/.test(trainLabel),
      'G1 选项应带方向与发车时间（实际 "' + trainLabel + '"）');
check(/北京/.test(trainLabel), 'G1 应标为北京→上海方向');

check($('s-count').textContent === '0', '初始售票数应为 0');

console.log('i18n 渲染');
check(doc.querySelector('[data-i18n="tabSell"]').textContent === '售票', '中文标签应已渲染');

console.log('售票流程');
$('f-id').value = '1234';           fire($('f-id'), 'input');
$('f-name').value = '测试姓名';       fire($('f-name'), 'input');
$('f-train').value = 'G2';          fire($('f-train'), 'change');
await wait(200);
check(/\d/.test($('f-price').textContent),
      '票价预览应显示数字（实际 "' + $('f-price').textContent + '"）');

fire($('sell-form'), 'submit');
await wait(400);
check(/购票成功/.test($('status').textContent),
      '状态栏应显示购票成功（实际 "' + $('status').textContent + '"）');
check($('pass-body').children.length === 1, '旅客表应出现 1 行');

const row = $('pass-body').children[0];
check(row.children.length === 8, '每行应有 8 列（实际 ' + row.children.length + '）');
check(row.children[1].textContent === '测试姓名', '姓名列应正确');

console.log('退票流程');
const refundBtn = row.querySelector('button');
check(refundBtn !== null, '每行应有退票按钮');
if (refundBtn) {
  fire(refundBtn, 'click');
  await wait(300);
  check($('pass-body').children.length === 0, '退票后表格应为空');
  check(/已退票/.test($('status').textContent), '状态栏应显示退票成功');
}

console.log('统计页');
fire(doc.querySelector('[data-view="stats"]'), 'click');
await wait(300);
// 统计按等级过滤：二等座只列车厢 3/4/5，一等座只列 1/2
check($('s-carriages').children.length === 3,
      '二等座应列出 3 个车厢（实际 ' + $('s-carriages').children.length + '）');
check($('s-segments').children.length === 5, '应有 5 个区段条');
$('s-class').value = '1'; fire($('s-class'), 'change');
await wait(200);
check($('s-carriages').children.length === 2,
      '切到一等座应列出 2 个车厢（实际 ' + $('s-carriages').children.length + '）');

console.log('语言切换');
fire($('lang-toggle'), 'click');
await wait(200);
check(doc.querySelector('[data-i18n="tabSell"]').textContent === 'Sell ticket', '应切换为英文');

check(errors.length === 0, '全过程不应有异常：' + errors.join(' | '));

console.log('\n================================');
console.log(checks + ' 项检查，' + failures + ' 项失败');
if (errors.length) console.log('\n异常详情:\n' + errors.join('\n---\n'));
process.exit(failures === 0 ? 0 : 1);
