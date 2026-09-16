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
// 默认站点是「上海→南京」（站号递增），车次会按方向过滤成偶数车次
check($('f-train').options.length === 5,
      '默认上海→南京，应列出 5 个偶数车次（实际 ' + $('f-train').options.length + '）');
check($('f-board').options.length === 6, '上车站下拉应有 6 项');
check($('f-alight').options.length === 6, '下车站下拉应有 6 项');
check($('f-class').options.length === 2, '等级下拉应有 2 项');

const trainLabel = $('f-train').options[0].textContent;
check(/G2/.test(trainLabel) && /06:30/.test(trainLabel),
      '首个候选应为 G2 06:30（实际 "' + trainLabel + '"）');
check(/上海→北京/.test(trainLabel), 'G2 应标为上海→北京方向');

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

console.log('旅客筛选');
// 先造几条记录，方便验证筛选
for (const [id, name] of [['2001', '甲'], ['2002', '乙'], ['3001', '丙']]) {
  $('f-id').value = id;          fire($('f-id'), 'input');
  $('f-name').value = name;      fire($('f-name'), 'input');
  $('f-train').value = 'G2';     fire($('f-train'), 'change');
  fire($('sell-form'), 'submit');
  await wait(40);
}
const rows = () => $('pass-body').children.length;
check(rows() === 3, '应先有 3 名旅客（实际 ' + rows() + '）');

$('pass-search').value = '200'; fire($('pass-search'), 'input');
await wait(60);
check(rows() === 2, '筛选 "200" 应剩 2 行（实际 ' + rows() + '）');
check(/匹配 2 \/ 共 3/.test($('pass-hint').textContent),
      '计数应显示「匹配 2 / 共 3」（实际 "' + $('pass-hint').textContent + '"）');

$('pass-search').value = '9999'; fire($('pass-search'), 'input');
await wait(60);
check(rows() === 0, '筛选无结果时应剩 0 行');
check($('pass-empty').hidden === false && /没有匹配/.test($('pass-empty').textContent),
      '无匹配时应显示「没有匹配的旅客」而不是「暂无旅客」');

$('pass-search').value = ''; fire($('pass-search'), 'input');
await wait(60);
check(rows() === 3, '清空筛选后应恢复 3 行');

console.log('车次下拉按方向与余票过滤');
const trainOptions = () => Array.from($('f-train').options).map((o) => o.value);

// 未选站点时应列出全部 10 个车次
$('f-board').value = ''; fire($('f-board'), 'change');
await wait(60);
check(trainOptions().length === 10,
      '未选站点时应列出全部车次（实际 ' + trainOptions().length + '）');

// 上海(0)→南京(2) 是站号递增，只应有偶数车次
$('f-board').value = '0'; fire($('f-board'), 'change');
$('f-alight').value = '2'; fire($('f-alight'), 'change');
$('f-class').value = '0'; fire($('f-class'), 'change');
await wait(120);
const southbound = trainOptions();
check(southbound.length === 5 && southbound.every((c) => parseInt(c.slice(1), 10) % 2 === 0),
      '上海→南京 应只剩偶数车次（实际 ' + southbound.join(',') + '）');

// 北京(5)→南京(2) 是站号递减，只应有奇数车次
$('f-board').value = '5'; fire($('f-board'), 'change');
await wait(120);
const northbound = trainOptions();
check(northbound.length === 5 && northbound.every((c) => parseInt(c.slice(1), 10) % 2 === 1),
      '北京→南京 应只剩奇数车次（实际 ' + northbound.join(',') + '）');

// 售罄的车次应从候选里消失：一等座只有 2 节车厢 × 8 座 = 16 个座位
$('f-board').value = '0'; fire($('f-board'), 'change');
$('f-alight').value = '2'; fire($('f-alight'), 'change');
$('f-class').value = '1'; fire($('f-class'), 'change');
await wait(120);
check(trainOptions().includes('G2'), '区间与等级选好后 G2 应在候选中');

for (let i = 0; i < 16; i++) {
  $('f-id').value = String(7000 + i); fire($('f-id'), 'input');
  $('f-name').value = 'F' + i;        fire($('f-name'), 'input');
  $('f-train').value = 'G2';          fire($('f-train'), 'change');
  fire($('sell-form'), 'submit');
  await wait(25);
}
check(!trainOptions().includes('G2'),
      'G2 一等座售罄后应从候选中消失（实际 ' + trainOptions().join(',') + '）');
check(trainOptions().includes('G4'), 'G4 仍有票，应保留在候选中');

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
