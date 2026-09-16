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

console.log('运行总览');
fire(doc.querySelector('[data-view="dashboard"]'), 'click');
await wait(200);
check($('dash-cards').children.length === 4,
      '总览应有 4 张卡片（实际 ' + $('dash-cards').children.length + '）');
check(/^\d+$/.test($('dash-cards').children[0].querySelector('.stat-value').textContent),
      '卡片数值应为数字');

console.log('车次列表');
fire(doc.querySelector('[data-view="timetable"]'), 'click');
await wait(250);
const ttRows = () => $('timetable-body').children.length;
check(ttRows() === 10, '车次列表应有 10 行（实际 ' + ttRows() + '）');
// 默认行程是 上海(0)→南京(2)：偶数车次同向，奇数车次方向不符
const seatCell = (i) => $('timetable-body').children[i].children[3].textContent;
check(seatCell(1) !== '—', 'G2 方向匹配，应显示余票（实际 "' + seatCell(1) + '"）');
check(seatCell(0) === '—', 'G1 方向不符，应显示 —（实际 "' + seatCell(0) + '"）');
check(/^\d{2}:\d{2}$/.test($('timetable-body').children[0].children[2].textContent),
      '发车时间格式应为 HH:MM');

console.log('主题切换');
const themeBtn = $('theme-toggle');
const rootEl = doc.documentElement;
check(!rootEl.hasAttribute('data-theme'), '默认跟随系统，不应写 data-theme');
fire(themeBtn, 'click'); await wait(60);
check(rootEl.getAttribute('data-theme') === 'light', '第一次点击应切到浅色');
fire(themeBtn, 'click'); await wait(60);
check(rootEl.getAttribute('data-theme') === 'dark', '第二次点击应切到深色');
fire(themeBtn, 'click'); await wait(60);
check(!rootEl.hasAttribute('data-theme'), '第三次点击应回到跟随系统');

console.log('排序与分页');
fire(doc.querySelector('[data-view="passengers"]'), 'click');
await wait(150);
check($('pass-body').children.length <= 15,
      '单页不应超过 15 行（实际 ' + $('pass-body').children.length + '）');
check(!$('pass-pager').hidden, '记录超过一页时应显示分页控件');
check($('pass-body').children[0].children[0].dataset.label === '证件',
      '手机卡片式布局依赖 data-label');

// 按票价排序（数值比较，结果确定）
const priceTh = doc.querySelector('.th-sort[data-sort-key="price"]');
fire(priceTh, 'click'); await wait(80);
check(priceTh.dataset.sort === 'asc', '首次点击表头应为升序');
let prices = Array.from($('pass-body').children).map((r) => parseInt(r.children[6].textContent, 10));
check(prices.every((v, i) => i === 0 || prices[i - 1] <= v), '按票价升序排列');

fire(priceTh, 'click'); await wait(80);
check(priceTh.dataset.sort === 'desc', '第二次点击应为降序');
prices = Array.from($('pass-body').children).map((r) => parseInt(r.children[6].textContent, 10));
check(prices.every((v, i) => i === 0 || prices[i - 1] >= v), '按票价降序排列');

fire(priceTh, 'click'); await wait(80);
check(!priceTh.dataset.sort, '第三次点击应清除排序标记');

// 翻页
fire($('page-next'), 'click'); await wait(80);
check($('page-prev').disabled === false, '第二页时「上一页」应可用');
check(/第 2 \//.test($('page-info').textContent),
      '页码应变为第 2 页（实际 "' + $('page-info').textContent + '"）');
fire($('page-prev'), 'click'); await wait(80);
check(/第 1 \//.test($('page-info').textContent), '应能翻回第 1 页');

console.log('语言切换');
fire($('lang-toggle'), 'click');
await wait(200);
check(doc.querySelector('[data-i18n="tabSell"]').textContent === 'Sell ticket', '应切换为英文');

check(errors.length === 0, '全过程不应有异常：' + errors.join(' | '));

console.log('\n================================');
console.log(checks + ' 项检查，' + failures + ' 项失败');
if (errors.length) console.log('\n异常详情:\n' + errors.join('\n---\n'));
process.exit(failures === 0 ? 0 : 1);
