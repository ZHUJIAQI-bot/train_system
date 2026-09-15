/* ============================================================
   网页版无头冒烟测试 —— 在 Node 里直接跑同一份 wasm，不需要浏览器。

   覆盖：JSON 转义、字段边界、区段复用、车厢人数平均、存档往返、
         损坏存档拒收（这是最危险的一条路径，开发期用干净小数据测不出来）。

   用法：node web/test/smoke.mjs   （需先构建，见 web/README.md）
   ============================================================ */
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const require = createRequire(import.meta.url);
const here = path.dirname(fileURLToPath(import.meta.url));
const distDir = path.join(here, '..', 'dist');

let createTrainModule;
try {
  createTrainModule = require(path.join(distDir, 'train_web.js'));
} catch (err) {
  console.error('无法加载 web/dist/train_web.js —— 请先运行 web/build.sh 或 web/build.bat');
  console.error(err.message);
  process.exit(2);
}

const ARCHIVE = '/passengers.dat';
const MAGIC = [0x54, 0x52, 0x4E, 0x50];   // "TRNP"
const RECORD_SIZE = 70;

let checks = 0;
let failures = 0;
function check(cond, what) {
  checks++;
  if (!cond) {
    failures++;
    console.log('  FAIL: ' + what);
  }
}

// 与前端一致：用本地日期，不用 toISOString（那是 UTC）
function localDate(offsetDays = 0) {
  const d = new Date();
  d.setDate(d.getDate() + offsetDays);
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`;
}

// ---- 手工构造成档字节，用来喂损坏用例 ----
function putFixed(buf, offset, text, size) {
  for (let i = 0; i < size; i++) buf[offset + i] = 0;
  for (let i = 0; i < text.length && i < size; i++) buf[offset + i] = text.charCodeAt(i) & 0xFF;
}
function putI32(buf, offset, value) {
  buf[offset]     = value & 0xFF;
  buf[offset + 1] = (value >> 8) & 0xFF;
  buf[offset + 2] = (value >> 16) & 0xFF;
  buf[offset + 3] = (value >> 24) & 0xFF;
}
function buildArchive(records) {
  const buf = new Uint8Array(16 + records.length * RECORD_SIZE);
  buf.set(MAGIC, 0);
  putI32(buf, 4, 4);                  // version
  putI32(buf, 8, records.length);     // count
  putI32(buf, 12, RECORD_SIZE);
  records.forEach((r, i) => {
    const base = 16 + i * RECORD_SIZE;
    putFixed(buf, base + 0,  r.id, 5);
    putFixed(buf, base + 5,  r.name, 20);
    putFixed(buf, base + 25, r.date, 11);
    putFixed(buf, base + 36, r.train, 4);
    putFixed(buf, base + 40, r.depart, 6);
    putI32(buf, base + 46, r.board);
    putI32(buf, base + 50, r.alight);
    putI32(buf, base + 54, r.price);
    putI32(buf, base + 58, r.carriage);
    putI32(buf, base + 62, r.seat);
    putI32(buf, base + 66, r.firstclass);
  });
  return buf;
}

// ---- 起一个全新的空状态：必须清掉 MEMFS 里的存档，否则会读回上一轮的数据 ----
async function freshModule() {
  const Module = await createTrainModule();
  try { Module.FS.unlink(ARCHIVE); } catch { /* 文件不存在是正常的 */ }
  const cw = (name, ret, args) => Module.cwrap(name, ret, args);
  const api = {
    state:     cw('api_state', 'string', []),
    options:   cw('api_options', 'string', []),
    sell:      cw('api_sell', 'string', ['string', 'string', 'string', 'string', 'number', 'number', 'number']),
    refund:    cw('api_refund', 'string', ['string']),
    carriages: cw('api_carriages', 'string', ['string', 'string', 'number']),
    segments:  cw('api_segments', 'string', ['string', 'string']),
    seatsLeft: cw('api_seats_left', 'string', ['string', 'string', 'number', 'number', 'number']),
    price:     cw('api_price', 'string', ['number', 'number', 'number']),
    load:      cw('api_load', 'string', ['string']),
    save:      cw('api_save', 'string', ['string']),
    expire:    cw('api_expire', 'string', ['string']),
    diagToday: cw('api_diagnostic_today', 'string', [])
  };
  return { Module, api };
}

const j = (s) => JSON.parse(s);

// ============================================================
async function main() {
  const { Module, api } = await freshModule();
  const today = localDate(0);
  const tomorrow = localDate(1);

  // ---- 1. 选项 ----
  console.log('接口与选项');
  const opts = j(api.options());
  check(opts.ok, 'api_options 应成功');
  check(opts.stations.length === 6, '应有 6 个车站');
  check(opts.trains.length === 10, '应有 10 个车次');
  check(opts.stations[0] === '上海', '首个车站应为上海（UTF-8 未被破坏）');
  check(opts.trains[0].code === 'G1' && opts.trains[0].northbound === 1,
        'G1 应为北京→上海方向（奇数车次）');
  check(opts.trains[1].code === 'G2' && opts.trains[1].northbound === 0,
        'G2 应为上海→北京方向（偶数车次）');
  check(opts.trains[0].depart === '06:00' && opts.trains[1].depart === '06:30',
        '发车时间应为 G1=06:00、G2=06:30');

  // 票价由 C 计算，前端不应在 JS 里重写规则
  check(j(api.price(0, 2, 0)).price === 300, '上海→南京 二等座 应为 300 元');
  check(j(api.price(0, 2, 1)).price === 400, '上海→南京 一等座 应为 400 元');
  check(j(api.price(5, 0, 0)).price === 750, '北京→上海 二等座 应为 750 元（反向应取绝对值）');
  check(!j(api.price(-1, 2, 0)).ok, '车站越界时 api_price 应失败');

  check(j(api.load(ARCHIVE)).result === 'empty', '无存档时 api_load 应报 empty');

  // ---- 2. 售票基础 ----
  console.log('售票与查重');
  let r = j(api.sell('1000', '张三', tomorrow, 'G2', 0, 2, 0));
  check(r.ok, '首张票应售出成功');
  check(r.carriage >= 3 && r.carriage <= 5, '二等座应落在车厢 3/4/5');
  check(r.price === 300, '上海→南京 二等座应为 300 元');

  r = j(api.sell('1000', '李四', tomorrow, 'G2', 0, 2, 0));
  check(!r.ok && r.code === 'duplicate', '同身份证应被拒（code=duplicate）');

  r = j(api.sell('1001', '张三', tomorrow, 'G1', 0, 2, 0));
  check(!r.ok && r.code === 'invalid_trip', 'G1 走上海→北京方向应被拒（车次奇偶不符）');

  r = j(api.sell('1002', '张三', tomorrow, 'G2', 2, 2, 0));
  check(!r.ok && r.code === 'invalid_trip', '上下车站相同应被拒');

  r = j(api.sell('12', '张三', tomorrow, 'G2', 0, 2, 0));
  check(!r.ok && r.code === 'invalid_id', '身份证位数不足应被拒');

  // ---- 3. JSON 转义与字段边界 ----
  console.log('姓名边界与 JSON 转义');
  r = j(api.sell('1003', 'a"b\\c', tomorrow, 'G2', 3, 5, 0));
  check(r.ok, '含引号与反斜杠的姓名应能售出');
  if (r.ok) {
    const found = j(api.state()).passengers.find((p) => p.id === '1003');
    check(found && found.name === 'a"b\\c', '姓名中的引号与反斜杠应原样往返');
  }

  // 先自校验测试数据本身，避免像最初那样把 7 个汉字误当成 6 个
  const sixChars = '六个汉字刚好';
  const sevenChars = '七个汉字就超了';
  check(Buffer.byteLength(sixChars, 'utf8') === 18, '测试用的 6 汉字串应为 18 字节');
  check(Buffer.byteLength(sevenChars, 'utf8') === 21, '测试用的 7 汉字串应为 21 字节');

  r = j(api.sell('1004', sixChars, tomorrow, 'G2', 3, 5, 0));
  check(r.ok, '18 字节（6 个汉字）的姓名应能售出');

  r = j(api.sell('1005', sevenChars, tomorrow, 'G2', 3, 5, 0));
  check(!r.ok && r.code === 'invalid_name', '21 字节（7 个汉字）应被拒，name[20] 只有 19 字节可用');

  r = j(api.sell('1006', '', tomorrow, 'G2', 3, 5, 0));
  check(!r.ok && r.code === 'invalid_name', '空姓名应被拒');

  // ---- 4. 区段复用 ----
  /* 注意：选厢策略是「人数最少的车厢」，所以两个区段不相交的旅客会先被分散到
     不同车厢，而不是立刻复用座位。要观察复用，需先把各车厢人数撑平。 */
  console.log('区段复用');
  const seg = await freshModule();
  const a = j(seg.api.sell('2000', 'A', tomorrow, 'G2', 0, 2, 0));
  check(a.ok && a.carriage === 3 && a.seat === 1, '首张应落在 车厢3 1号座');

  const b = j(seg.api.sell('2001', 'B', tomorrow, 'G2', 0, 2, 0));
  check(b.ok && b.carriage === 4, '第二张应落到人数更少的车厢4（平均分配）');

  const c = j(seg.api.sell('2002', 'C', tomorrow, 'G2', 0, 2, 0));
  check(c.ok && c.carriage === 5, '第三张应落到车厢5，三节铺平');

  // 此刻 3/4/5 各 1 人，人数持平，再卖一张不相交区段的票会回到车厢3 的 1 号座
  const d = j(seg.api.sell('2003', 'D', tomorrow, 'G2', 3, 5, 0));
  check(d.ok, '人数持平时第四张应能售出');
  check(d.carriage === 3 && d.seat === 1,
        '各厢人数持平时，区段不相交应复用 车厢3 1号座（实际 ' + d.carriage + '/' + d.seat + '）');

  // 区段相交则绝不能复用
  const e = j(seg.api.sell('2004', 'E', tomorrow, 'G2', 1, 4, 0));
  check(e.ok, '区段相交的票应售出');
  check(!(e.carriage === 3 && e.seat === 1), '区段相交不能分到已被占用的座位');

  // 奇数车次的反向区间（朴素重叠公式 b1<a2 && a1>b2 在这里会误判为不重叠）
  const odd = await freshModule();
  const oa = j(odd.api.sell('3000', 'A', tomorrow, 'G1', 5, 0, 0));   // 北京→上海 全程
  const ob = j(odd.api.sell('3001', 'B', tomorrow, 'G1', 3, 0, 0));   // 济南→上海，与之重叠
  check(oa.ok && ob.ok, 'G1 两张票都应售出');
  check(!(oa.carriage === ob.carriage && oa.seat === ob.seat),
        'G1 区段重叠不能复用同一座位（奇数车次回归）');

  // ---- 5. 车厢人数平均分配 ----
  console.log('车厢人数平均分配');
  const dist = await freshModule();
  const seats = [];
  for (let i = 0; i < 6; i++) {
    const res = j(dist.api.sell(String(4000 + i), 'P' + i, tomorrow, 'G2', 0, 5, 0));
    check(res.ok, '第 ' + (i + 1) + ' 张应售出');
    seats.push(res.carriage);
  }
  check(seats.slice(0, 3).sort().join(',') === '3,4,5', '前 3 张应铺满 3/4/5 三节车厢');
  check(seats[3] === seats[0], '第 4 张应回到第 1 张所在的车厢（轮转）');

  const cars = j(dist.api.carriages('G2', tomorrow, 0));
  const heads = cars.carriages.map((x) => x.headcount);
  check(Math.max(...heads) - Math.min(...heads) <= 1, '各车厢人数差应不超过 1');

  // ---- 6. 存档往返 ----
  console.log('存档往返');
  const before = j(dist.api.state());
  check(j(dist.api.save(ARCHIVE)).ok, 'api_save 应成功');
  // 必须用 dist.Module：每个 wasm 实例有各自独立的线性内存与 MEMFS，
  // 读顶层 Module 的文件系统会 ENOENT（存档是写到 dist 那个实例里的）
  const bytes = dist.Module.FS.readFile(ARCHIVE);
  check(bytes.length === 16 + before.count * RECORD_SIZE,
        '文件长度应为 16 + n×70（实际 ' + bytes.length + '，n=' + before.count + '）');
  check(MAGIC.every((m, i) => bytes[i] === m), '存档魔数应为 TRNP');

  const reload = await freshModule();
  reload.Module.FS.writeFile(ARCHIVE, bytes);
  const loaded = j(reload.api.load(ARCHIVE));
  check(loaded.ok && loaded.result === 'loaded', '重新加载应成功');
  const after = j(reload.api.state());
  check(after.count === before.count, '往返后记录数应一致');
  check(JSON.stringify(after.passengers) === JSON.stringify(before.passengers),
        '往返后每条记录应逐字段一致');

  // ---- 7. 退票 ----
  console.log('退票');
  const rf = j(reload.api.refund(before.passengers[0].id));
  check(rf.ok, '退票应成功');
  check(rf.name === before.passengers[0].name, '退票返回的姓名应正确（不能是已释放内存）');
  check(j(reload.api.state()).count === before.count - 1, '退票后记录数应减 1');
  check(!j(reload.api.refund('9999')).ok, '退不存在的票应失败');

  // ---- 8. 损坏存档拒收 ----
  console.log('损坏存档拒收');
  const bad = await freshModule();

  // board=99 —— 旧代码会在这里 board_cnt[99]++ 栈越界
  let rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '06:30',
    board: 99, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0
  }]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  let res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', 'board=99 的存档应被拒收');

  // 车厢与等级不符
  rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '06:30',
    board: 0, alight: 2, price: 300, carriage: 1, seat: 1, firstclass: 0
  }]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '二等座旅客放进一等座车厢应被拒收');

  // 发车时间与车次不符
  rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '09:99',
    board: 0, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0
  }]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '发车时间与车次不符应被拒收');

  // 篡改 price：应被接受，但读入后按规则重算覆盖
  rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '06:30',
    board: 0, alight: 2, price: 99999, carriage: 3, seat: 1, firstclass: 0
  }]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(res.ok, 'price 被篡改仍应接受该记录');
  check(j(bad.api.state()).passengers[0].price === 300, '读入后 price 应被重算为 300 而不是 99999');

  // 两条记录同座位且区段重叠
  rec = buildArchive([
    { id: '8888', name: 'A', date: tomorrow, train: 'G2', depart: '06:30',
      board: 0, alight: 5, price: 750, carriage: 3, seat: 1, firstclass: 0 },
    { id: '8889', name: 'B', date: tomorrow, train: 'G2', depart: '06:30',
      board: 1, alight: 4, price: 450, carriage: 3, seat: 1, firstclass: 0 }
  ]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '同座位且区段重叠应被拒收');

  // 两条记录同座位但区段不相交：必须接受（防止把合法复用误判为冲突）
  rec = buildArchive([
    { id: '8888', name: 'A', date: tomorrow, train: 'G2', depart: '06:30',
      board: 0, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0 },
    { id: '8889', name: 'B', date: tomorrow, train: 'G2', depart: '06:30',
      board: 3, alight: 5, price: 300, carriage: 3, seat: 1, firstclass: 0 }
  ]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(res.ok, '同座位但区段不相交应被接受');
  check(j(bad.api.state()).count === 2, '应读入 2 条');

  // 身份证重复
  rec = buildArchive([
    { id: '8888', name: 'A', date: tomorrow, train: 'G2', depart: '06:30',
      board: 0, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0 },
    { id: '8888', name: 'B', date: tomorrow, train: 'G2', depart: '06:30',
      board: 3, alight: 5, price: 300, carriage: 3, seat: 2, firstclass: 0 }
  ]);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '身份证重复应被拒收');

  // 截断文件
  rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '06:30',
    board: 0, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0
  }]).slice(0, 40);
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '截断文件应被拒收');

  // 魔数不对
  rec = buildArchive([{
    id: '7777', name: 'T', date: tomorrow, train: 'G2', depart: '06:30',
    board: 0, alight: 2, price: 300, carriage: 3, seat: 1, firstclass: 0
  }]);
  rec[0] = 0x58;
  bad.Module.FS.writeFile(ARCHIVE, rec);
  res = j(bad.api.load(ARCHIVE));
  check(!res.ok && res.code === 'rejected', '魔数错误应被拒收');

  // ---- 9. 日期与时区 ----
  console.log('日期');
  const diag = j(api.diagToday());
  check(diag.ok && /^\d{4}-\d{2}-\d{2}$/.test(diag.today), 'C 侧应能报告一个合法日期');
  if (diag.today !== today) {
    console.log('  注意：C 的 localtime 给出 ' + diag.today + '，JS 本地日期是 ' + today +
                '（时区差异，业务逻辑已改为以 JS 为准）');
  }

  const exp = await freshModule();
  exp.api.sell('5000', 'X', tomorrow, 'G2', 0, 2, 0);
  const expRes = j(exp.api.expire(today));
  check(expRes.ok && expRes.removed === 0, '未来日期的票不应被当作过期清理');

  // ---- 汇总 ----
  console.log('\n================================');
  console.log(checks + ' 项检查，' + failures + ' 项失败');
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((err) => {
  console.error('测试异常终止：', err);
  process.exit(2);
});
