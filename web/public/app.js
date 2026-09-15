/* ============================================================
   网页版前端 —— 所有业务逻辑都在 C（编译成 wasm），这里只负责界面与持久化。
   ============================================================ */
(function () {
  'use strict';

  // ---------------- 常量 ----------------
  const ARCHIVE_PATH = '/passengers.dat';
  const LS_ARCHIVE   = 'train.v1.archive';        // base64
  const LS_SEQ       = 'train.v1.seq';            // 单调计数，用于多标签页冲突检测
  const LS_REJECTED  = 'train.v1.rejected.';      // + 时间戳
  const LS_BEFORE_EXPIRE = 'train.v1.beforeExpire';
  const DATE_OPTIONS = 4;
  const MAGIC = [0x54, 0x52, 0x4E, 0x50];         // "TRNP"

  const STATIONS_EN = ['Shanghai', 'Suzhou', 'Nanjing', 'Jinan', 'Tianjin', 'Beijing'];

  // ---------------- i18n ----------------
  const I18N = {
    zh: {
      brand: '中国铁路', brandSub: '旅客管理系统 · 网页版',
      tabSell: '售票', tabPassengers: '旅客列表', tabStats: '统计信息',
      sellTitle: '售票', sellHint: '填写旅客信息，系统自动分配座位。',
      fId: '身份证后 4 位', fIdNote: '4 位半角数字，末位可为 x',
      fName: '姓名', fDate: '出行日期', fTrain: '车次',
      fBoard: '上车站', fAlight: '下车站', fClass: '座位等级', fPrice: '票价',
      classSecond: '二等座', classFirst: '一等座', submitSell: '确认购票',
      passTitle: '旅客列表', passEmpty: '暂无旅客。',
      thId: '证件', thName: '姓名', thDate: '日期', thTrain: '车次',
      thRoute: '行程', thSeat: '车厢/座位', thPrice: '票价', thAction: '操作',
      statsTitle: '统计信息', statsHint: '按车次与日期查看座位占用与区段载客。',
      statTotal: '售票总数', unitTickets: '张', statFare: '票款合计', unitYuan: '元',
      carriageTitle: '各车厢座位', segmentTitle: '各区段载客',
      exportArchive: '导出存档', importArchive: '导入存档',
      refund: '退票', passCount: '共 {n} 名旅客',
      seatFree: '空闲 {n}', seatUsed: '占用 {n}', headcount: '人数 {n}',
      segLoad: '{n} / {m} 人',
      errIdLength: '身份证后 4 位应为 4 位半角数字（末位可为 x）',
      errNameEmpty: '请输入姓名',
      errNameLong: '姓名过长：最多 19 字节（约 6 个汉字）',
      errSameStation: '上车站与下车站不能相同',
      errNoStation: '请选择上车站和下车站',
      seatsLeft: '该区间剩余 {n} 张',
      seatSoldOut: '该区间该等级已售罄',
      price: '票价 {n} 元',
      sellOk: '购票成功：{c} 号车厢 {s} 号座位，票价 {p} 元',
      refundOk: '旅客 {name} 已退票',
      netErr: '操作失败：{msg}',
      bannerRejectedTitle: '存档未通过校验，已停止写入以保护原始数据',
      bannerRejectedDetail: '原始存档已另存一份。你可以先导出保存，再决定是否重新开始。',
      bannerExport: '导出原始存档', bannerRestart: '放弃并新建',
      bannerStorageTitle: '浏览器存储不可用',
      bannerStorageDetail: '本次改动只在内存中，刷新页面会丢失。请用「导出存档」保存。',
      bannerDismiss: '知道了',
      warnClock: '系统日期看起来不正确，已跳过过期票清理以免误删。',
      warnExpired: '已自动清理 {n} 张过期车票（清理前的存档已备份）。',
      warnSaveFailed: '保存失败：本次改动仅在内存中，刷新将丢失。',
      warnTabConflict: '另一个标签页修改了数据，本页显示的可能已过时。',
      nothingToExport: '没有可导出的数据。',
      importBadMagic: '文件不是本系统的存档格式（缺少 TRNP 标识）。',
      importRejected: '该存档未通过校验，已拒绝导入。',
      imported: '存档已导入。',
      emptyArchive: '（空）',
      confirmRestart: '确定放弃当前存档并从空数据重新开始吗？此操作不可撤销。'
    },
    en: {
      brand: 'CHINA RAILWAY', brandSub: 'Passenger Desk · Web',
      tabSell: 'Sell ticket', tabPassengers: 'Passengers', tabStats: 'Statistics',
      sellTitle: 'Sell ticket', sellHint: 'Fill in the details; a seat is assigned automatically.',
      fId: 'ID suffix (4 chars)', fIdNote: '4 half-width digits, last may be x',
      fName: 'Name', fDate: 'Travel date', fTrain: 'Train',
      fBoard: 'Board at', fAlight: 'Alight at', fClass: 'Seat class', fPrice: 'Fare',
      classSecond: 'Second class', classFirst: 'First class', submitSell: 'Confirm ticket',
      passTitle: 'Passengers', passEmpty: 'No passengers yet.',
      thId: 'ID', thName: 'Name', thDate: 'Date', thTrain: 'Train',
      thRoute: 'Route', thSeat: 'Carriage/Seat', thPrice: 'Fare', thAction: 'Action',
      statsTitle: 'Statistics', statsHint: 'Seat occupancy and per-segment load.',
      statTotal: 'Tickets sold', unitTickets: '', statFare: 'Total fare', unitYuan: 'CNY',
      carriageTitle: 'Seats by carriage', segmentTitle: 'Passengers per segment',
      exportArchive: 'Export save', importArchive: 'Import save',
      refund: 'Refund', passCount: '{n} passenger(s) on record',
      seatFree: '{n} free', seatUsed: '{n} used', headcount: '{n} people',
      segLoad: '{n} / {m}',
      errIdLength: 'ID suffix must be 4 half-width digits (last may be x)',
      errNameEmpty: 'Name is required',
      errNameLong: 'Name too long: 19 bytes max (about 6 Chinese characters)',
      errSameStation: 'Board and alight stations must differ',
      errNoStation: 'Select both board and alight stations',
      seatsLeft: '{n} seat(s) left on this leg',
      seatSoldOut: 'No seats left for this class on this leg',
      price: 'Fare {n} CNY',
      sellOk: 'Ticket confirmed: carriage {c}, seat {s}, fare {p} CNY',
      refundOk: 'Passenger {name} refunded',
      netErr: 'Failed: {msg}',
      bannerRejectedTitle: 'Save file failed validation — writing is disabled to protect your data',
      bannerRejectedDetail: 'The original has been kept aside. Export it first, then decide whether to start over.',
      bannerExport: 'Export original', bannerRestart: 'Discard and start over',
      bannerStorageTitle: 'Browser storage unavailable',
      bannerStorageDetail: 'Changes live in memory only and will be lost on reload. Use "Export save".',
      bannerDismiss: 'Dismiss',
      warnClock: 'System date looks wrong — skipped expired-ticket cleanup to avoid deleting data.',
      warnExpired: 'Removed {n} expired ticket(s); the previous save was backed up.',
      warnSaveFailed: 'Save failed: changes are in memory only and will be lost on reload.',
      warnTabConflict: 'Another tab changed the data; this page may be out of date.',
      nothingToExport: 'Nothing to export.',
      importBadMagic: 'Not a valid save file (missing TRNP signature).',
      importRejected: 'That save file failed validation and was not imported.',
      imported: 'Save file imported.',
      emptyArchive: '(empty)',
      confirmRestart: 'Discard the current save and start from empty? This cannot be undone.'
    }
  };

  let lang = 'zh';
  const t = (key) => (I18N[lang][key] !== undefined ? I18N[lang][key] : key);
  const fmt = (key, vars) => t(key).replace(/\{(\w+)\}/g, (_, k) => (vars[k] !== undefined ? vars[k] : ''));

  // ---------------- 运行时状态 ----------------
  let Module = null;
  let api = {};
  let state = { passengers: [], count: 0, totalFare: 0 };
  let options = { stations: [], trains: [], carriages: [], segmentCount: 5 };

  let circuitBroken = false;      // 熔断：禁止一切写回
  let rejectedBlob = null;        // 被拒收的原始存档，用于导出
  let storageAvailable = true;    // localStorage 是否可用
  let knownSeq = 0;               // 多标签页冲突检测

  // ---------------- 日期：一律按本地时区算 ----------------
  /* 绝不能用 toISOString().slice(0,10) —— 那给的是 UTC 日期，
     UTC+8 用户在凌晨 0-8 点会拿到「昨天」，导致今天买的票被当成过期。 */
  function localDateString(d) {
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, '0');
    const day = String(d.getDate()).padStart(2, '0');
    return y + '-' + m + '-' + day;
  }
  function todayString() { return localDateString(new Date()); }
  function dateOption(i) {
    const d = new Date();
    d.setDate(d.getDate() + i);      // Date 的算术自带 DST 正确性
    return localDateString(d);
  }

  // ---------------- base64：必须分块 ----------------
  /* String.fromCharCode.apply(null, u8) 在 100KB 以上会
     RangeError: Maximum call stack size exceeded，必须按 8KB 分块。 */
  function bytesToBase64(bytes) {
    const CHUNK = 0x2000;
    let out = '';
    for (let i = 0; i < bytes.length; i += CHUNK) {
      out += String.fromCharCode.apply(null, bytes.subarray(i, i + CHUNK));
    }
    return btoa(out);
  }
  function base64ToBytes(b64) {
    const bin = atob(b64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }
  function hasMagic(bytes) {
    if (!bytes || bytes.length < 16) return false;
    for (let i = 0; i < 4; i++) if (bytes[i] !== MAGIC[i]) return false;
    return true;
  }

  // ---------------- 存储 ----------------
  function lsGet(key) {
    try { return localStorage.getItem(key); } catch (e) { return null; }
  }
  function lsSet(key, value) {
    try { localStorage.setItem(key, value); return true; }
    catch (e) { storageAvailable = false; showStorageBanner(); return false; }
  }

  function readSeq() {
    const raw = lsGet(LS_SEQ);
    const n = parseInt(raw || '0', 10);
    return Number.isFinite(n) ? n : 0;
  }

  /* 把存档写回 localStorage。
     注意：api_save 失败时**绝不读 FS** —— 失败路径已经把临时文件删了，
     此时 /passengers.dat 可能是旧内容甚至不存在，读出来是脏的。 */
  function persist() {
    if (circuitBroken) return true;             // 熔断态：一律不写
    if (!storageAvailable) return true;         // 只落到 MEMFS

    const saved = JSON.parse(api.save(ARCHIVE_PATH));
    if (!saved.ok) {
      setStatus(t('warnSaveFailed'), true);
      return false;
    }
    let bytes;
    try { bytes = Module.FS.readFile(ARCHIVE_PATH); }
    catch (e) { setStatus(t('warnSaveFailed'), true); return false; }

    const currentSeq = readSeq();
    if (currentSeq !== knownSeq) {
      // 另一个标签页在我们读入之后写过东西，直接覆盖会冲掉它的改动
      setStatus(t('warnTabConflict'), true);
      knownSeq = currentSeq;
      return false;
    }
    knownSeq = currentSeq + 1;
    lsSet(LS_SEQ, String(knownSeq));
    lsSet(LS_ARCHIVE, bytesToBase64(bytes));
    return true;
  }

  function isolateRejected(b64) {
    try { localStorage.setItem(LS_REJECTED + Date.now(), b64); } catch (e) { /* 尽力而为 */ }
  }

  // ---------------- 界面辅助 ----------------
  function setStatus(text, isError) {
    const el = document.getElementById('status');
    el.textContent = text || '';
    el.classList.toggle('is-error', !!isError);
  }
  function showBanner(titleKey, detailKey, allowRestart) {
    document.getElementById('banner-title').textContent = t(titleKey);
    document.getElementById('banner-detail').textContent = t(detailKey);
    document.getElementById('banner-export').textContent = t('bannerExport');
    document.getElementById('banner-dismiss').textContent = t('bannerDismiss');
    // 「放弃并新建」只在存档被拒收时有意义；存储不可用时给这个按钮会误导人
    document.getElementById('banner-restart').hidden = !allowRestart;
    document.getElementById('banner-restart').textContent = t('bannerRestart');
    document.getElementById('banner').hidden = false;
  }
  function hideBanner() { document.getElementById('banner').hidden = true; }
  function showStorageBanner() { showBanner('bannerStorageTitle', 'bannerStorageDetail', false); }

  function stationName(index) {
    return lang === 'zh' ? (options.stations[index] || '?') : (STATIONS_EN[index] || '?');
  }
  function fillSelect(sel, items) {
    sel.textContent = '';
    items.forEach((item) => {
      const opt = document.createElement('option');
      opt.value = String(item.value);
      opt.textContent = item.label;
      sel.appendChild(opt);
    });
  }

  // ---------------- 初始化下拉框 ----------------
  function buildSelects() {
    const stationItems = options.stations.map((_, i) => ({
      value: i, label: i + '  ' + stationName(i)
    }));
    const trainItems = options.trains.map((code) => {
      const n = parseInt(code.slice(1), 10);
      const northbound = n % 2 === 1;                  // 奇数车次 北京→上海
      const hour = northbound ? 6 + (n - 1) / 2 : 6 + (n - 2) / 2;
      const time = String(hour).padStart(2, '0') + (northbound ? ':00' : ':30');
      const dir = lang === 'zh'
        ? (northbound ? '北京→上海' : '上海→北京')
        : (northbound ? 'Beijing→Shanghai' : 'Shanghai→Beijing');
      return { value: code, label: code + '  ' + dir + ' ' + time };
    });
    const dateItems = [];
    for (let i = 0; i < DATE_OPTIONS; i++) {
      const d = dateOption(i);
      dateItems.push({ value: d, label: d + (i === 0 ? (lang === 'zh' ? '（今天）' : ' (today)') : '') });
    }

    fillSelect(document.getElementById('f-date'), dateItems);
    fillSelect(document.getElementById('s-date'), dateItems);
    fillSelect(document.getElementById('f-train'), trainItems);
    fillSelect(document.getElementById('s-train'), trainItems);
    fillSelect(document.getElementById('s-class'), [
      { value: 0, label: t('classSecond') }, { value: 1, label: t('classFirst') }
    ]);

    // 站点的选项标签依赖语言，切换语言时要重建
    fillSelect(document.getElementById('f-board'), stationItems);
    fillSelect(document.getElementById('f-alight'), stationItems);
    document.getElementById('f-alight').value = String(Math.min(2, options.stations.length - 1));
  }

  // ---------------- 售票表单 ----------------
  /* select 的 value 是空串时 parseInt('') 得到 NaN，经 cwrap 的 'number' 传进 C
     会变成 0，而 0 既是合法的「上海」也是合法的「二等座」—— 空选择会被静默
     当成一笔合法订单。所以取值一律走这个函数，返回 -1 让 validate_trip 兜住。 */
  function selectInt(id) {
    const raw = document.getElementById(id).value;
    if (raw === '') return -1;
    const n = parseInt(raw, 10);
    return Number.isInteger(n) ? n : -1;
  }
  function selectStr(id) { return document.getElementById(id).value || ''; }

  function currentForm() {
    return {
      id: document.getElementById('f-id').value.trim(),
      name: document.getElementById('f-name').value,
      date: selectStr('f-date'),
      train: selectStr('f-train'),
      board: selectInt('f-board'),
      alight: selectInt('f-alight'),
      firstclass: selectInt('f-class')
    };
  }

  // 姓名上限是 19 字节，按 UTF-8 字节数而不是字符数算
  function utf8Length(s) {
    let n = 0;
    for (let i = 0; i < s.length; i++) {
      const c = s.codePointAt(i);
      if (c > 0xFFFF) i++;
      n += c <= 0x7F ? 1 : c <= 0x7FF ? 2 : c <= 0xFFFF ? 3 : 4;
    }
    return n;
  }

  function refreshFormHints() {
    const f = currentForm();
    const nameNote = document.getElementById('name-note');
    const bytes = utf8Length(f.name);
    nameNote.textContent = f.name ? bytes + ' / 19 bytes' : '';
    nameNote.classList.toggle('is-error', bytes > 19);

    const trainNote = document.getElementById('train-note');
    if (f.board < 0 || f.alight < 0 || f.board === f.alight) {
      trainNote.textContent = '';
    } else {
      const res = JSON.parse(api.seatsLeft(f.train, f.date, f.board, f.alight, f.firstclass));
      trainNote.textContent = res.ok ? fmt('seatsLeft', { n: res.seats }) : '';
      trainNote.classList.toggle('is-error', res.ok && res.seats === 0);
    }

    const priceOut = document.getElementById('f-price');
    if (f.board < 0 || f.alight < 0 || f.board === f.alight) {
      priceOut.textContent = '—';
    } else {
      const seg = Math.abs(f.alight - f.board);
      priceOut.textContent = fmt('price', { n: seg * 150 + (f.firstclass === 1 ? 100 : 0) });
    }
  }

  function onSubmitSell(ev) {
    ev.preventDefault();
    const f = currentForm();

    if (!/^[0-9]{3}[0-9xX]$/.test(f.id)) { return setStatus(t('errIdLength'), true); }
    if (f.name.length === 0) { return setStatus(t('errNameEmpty'), true); }
    if (utf8Length(f.name) > 19) { return setStatus(t('errNameLong'), true); }
    if (f.board < 0 || f.alight < 0) { return setStatus(t('errNoStation'), true); }
    if (f.board === f.alight) { return setStatus(t('errSameStation'), true); }

    const res = JSON.parse(api.sell(f.id, f.name, f.date, f.train, f.board, f.alight, f.firstclass));
    if (!res.ok) { return setStatus(res.message || fmt('netErr', { msg: res.code }), true); }

    persist();
    refresh();
    setStatus(fmt('sellOk', { c: res.carriage, s: res.seat, p: res.price }));
    document.getElementById('f-id').value = '';
    document.getElementById('f-name').value = '';
    refreshFormHints();
  }

  // ---------------- 退票 ----------------
  function doRefund(id) {
    const res = JSON.parse(api.refund(id));
    if (!res.ok) { return setStatus(res.message, true); }
    persist();
    refresh();
    setStatus(fmt('refundOk', { name: res.name }));
  }

  // ---------------- 渲染 ----------------
  function cell(text) {
    const td = document.createElement('td');
    td.textContent = text;      // 一律 textContent：绝不把数据塞进 innerHTML
    return td;
  }

  function renderPassengers() {
    const body = document.getElementById('pass-body');
    body.textContent = '';
    state.passengers.forEach((p) => {
      const tr = document.createElement('tr');
      tr.appendChild(cell(p.id));
      tr.appendChild(cell(p.name));
      tr.appendChild(cell(p.date));
      tr.appendChild(cell(p.train));
      tr.appendChild(cell(stationName(p.board) + ' → ' + stationName(p.alight)));
      tr.appendChild(cell(p.carriage + ' / ' + p.seat));
      const fare = cell(String(p.price));
      fare.className = 'num';
      tr.appendChild(fare);

      const action = document.createElement('td');
      action.className = 'num';
      const btn = document.createElement('button');
      btn.className = 'btn-ghost small';
      btn.textContent = t('refund');
      btn.addEventListener('click', () => doRefund(p.id));
      action.appendChild(btn);
      tr.appendChild(action);

      body.appendChild(tr);
    });
    document.getElementById('pass-empty').hidden = state.passengers.length > 0;
    document.getElementById('pass-hint').textContent = fmt('passCount', { n: state.count });
    document.getElementById('s-count').textContent = String(state.count);
    document.getElementById('s-fare').textContent = String(state.totalFare);
  }

  function barRow(label, fillRatio, detail, warn) {
    const row = document.createElement('div');
    row.className = 'bar-row';

    const name = document.createElement('span');
    name.textContent = label;

    const track = document.createElement('div');
    track.className = 'bar-track';
    const fill = document.createElement('div');
    fill.className = 'bar-fill' + (warn ? ' warn' : '');
    fill.style.width = Math.max(0, Math.min(1, fillRatio)) * 100 + '%';
    track.appendChild(fill);

    const detailEl = document.createElement('span');
    detailEl.className = 'bar-label';
    detailEl.textContent = detail;

    row.appendChild(name);
    row.appendChild(track);
    row.appendChild(detailEl);
    return row;
  }

  function renderStats() {
    const train = document.getElementById('s-train').value;
    const date = document.getElementById('s-date').value;
    const firstclass = selectInt('s-class');

    const cars = JSON.parse(api.carriages(train, date, firstclass));
    const carBox = document.getElementById('s-carriages');
    carBox.textContent = '';
    if (cars.ok) {
      cars.carriages.forEach((c) => {
        const label = (lang === 'zh' ? c.index + ' 号车厢' : 'Carriage ' + c.index);
        const detail = fmt('seatFree', { n: c.free }) + ' · ' +
                       fmt('seatUsed', { n: c.occupied }) + ' · ' +
                       fmt('headcount', { n: c.headcount });
        carBox.appendChild(barRow(label, c.occupied / c.seats, detail, c.occupied >= c.seats));
      });
    }

    const segs = JSON.parse(api.segments(train, date));
    const segBox = document.getElementById('s-segments');
    segBox.textContent = '';
    if (segs.ok) {
      segs.segments.forEach((s) => {
        const label = stationName(s.from) + '-' + stationName(s.to);
        segBox.appendChild(barRow(label, s.load / segs.capacity,
                                  fmt('segLoad', { n: s.load, m: segs.capacity }),
                                  s.load >= segs.capacity));
      });
    }
  }

  function refresh() {
    state = JSON.parse(api.state());
    renderPassengers();
    renderStats();
    refreshFormHints();
  }

  // ---------------- 存档导入导出 ----------------
  function exportArchive() {
    let bytes = null;
    if (circuitBroken && rejectedBlob) {
      bytes = rejectedBlob;               // 熔断时导出被拒收的原始存档
    } else {
      try { bytes = Module.FS.readFile(ARCHIVE_PATH); } catch (e) { bytes = null; }
    }
    if (!bytes || bytes.length === 0) { return setStatus(t('nothingToExport'), true); }

    const blob = new Blob([bytes], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'passengers-' + todayString() + '.dat';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  async function importArchive(file) {
    const buf = new Uint8Array(await file.arrayBuffer());
    if (!hasMagic(buf)) { return setStatus(t('importBadMagic'), true); }

    Module.FS.writeFile(ARCHIVE_PATH, buf);
    const res = JSON.parse(api.load(ARCHIVE_PATH));
    if (!res.ok) { return setStatus(t('importRejected'), true); }

    // 导入成功即解除熔断，并把这份存档写回 localStorage
    circuitBroken = false;
    rejectedBlob = null;
    hideBanner();
    persist();
    refresh();
    setStatus(t('imported'));
  }

  // ---------------- 过期票清理 ----------------
  /* remove_expired_passengers 会按日期全量删除，而浏览器里唯一副本就是
     localStorage。用户时钟错、跨时区、书签放了很久，都可能让它误删。
     这里加三道锁，都很便宜。 */
  function purgeExpired() {
    const today = todayString();
    const year = parseInt(today.slice(0, 4), 10);
    if (year < 2020 || year > 2099) { setStatus(t('warnClock'), true); return; }

    // 时钟倒流检测：今天的日期早于任何一张票的出行日期，说明系统时间不对
    let maxDate = '';
    state.passengers.forEach((p) => { if (p.date > maxDate) maxDate = p.date; });
    if (maxDate && today < maxDate) { setStatus(t('warnClock'), true); return; }

    const res = JSON.parse(api.expire(today));
    if (res.ok && res.removed > 0) {
      // 清理前先备份，给用户一次后悔的机会
      const before = lsGet(LS_ARCHIVE);
      if (before) lsSet(LS_BEFORE_EXPIRE, before);
      persist();
      setStatus(fmt('warnExpired', { n: res.removed }));
      refresh();
    }
  }

  // ---------------- 启动 ----------------
  async function boot() {
    Module = await createTrainModule();

    api = {
      state:      Module.cwrap('api_state', 'string', []),
      options:    Module.cwrap('api_options', 'string', []),
      sell:       Module.cwrap('api_sell', 'string',
                    ['string', 'string', 'string', 'string', 'number', 'number', 'number']),
      refund:     Module.cwrap('api_refund', 'string', ['string']),
      carriages:  Module.cwrap('api_carriages', 'string', ['string', 'string', 'number']),
      segments:   Module.cwrap('api_segments', 'string', ['string', 'string']),
      seatsLeft:  Module.cwrap('api_seats_left', 'string',
                    ['string', 'string', 'number', 'number', 'number']),
      load:       Module.cwrap('api_load', 'string', ['string']),
      save:       Module.cwrap('api_save', 'string', ['string']),
      expire:     Module.cwrap('api_expire', 'string', ['string']),
      diagToday:  Module.cwrap('api_diagnostic_today', 'string', [])
    };

    options = JSON.parse(api.options());
    knownSeq = readSeq();
    buildSelects();

    // --- 载入存档 ---
    const stored = lsGet(LS_ARCHIVE);
    if (stored) {
      let bytes = null;
      try { bytes = base64ToBytes(stored); } catch (e) { bytes = null; }

      if (!hasMagic(bytes)) {
        // 连魔数都不对，不必交给 C 去判 —— 直接走熔断，保住原始数据
        rejectedBlob = bytes;
        circuitBroken = true;
        isolateRejected(stored);
        showBanner('bannerRejectedTitle', 'bannerRejectedDetail', true);
      } else {
        Module.FS.writeFile(ARCHIVE_PATH, bytes);
        const res = JSON.parse(api.load(ARCHIVE_PATH));
        if (!res.ok) {
          rejectedBlob = bytes;
          circuitBroken = true;
          isolateRejected(stored);
          showBanner('bannerRejectedTitle', 'bannerRejectedDetail', true);
        }
      }
    } else {
      // 没有存档：让 C 走一遍空加载路径，确认它处于 "empty" 状态
      api.load(ARCHIVE_PATH);
    }

    refresh();
    if (!circuitBroken) purgeExpired();
    refresh();

    // --- 时区自检：C 的 localtime 与 JS 本地日期不一致就告警 ---
    const diag = JSON.parse(api.diagToday());
    if (diag.ok && diag.today !== todayString()) {
      console.warn('C 视角的今天与浏览器本地日期不一致：', diag.today, 'vs', todayString());
    }

    bindEvents();
    setStatus(circuitBroken ? '' : t('passCount').replace('{n}', state.count));
  }

  function bindEvents() {
    document.getElementById('sell-form').addEventListener('submit', onSubmitSell);
    document.getElementById('export-btn').addEventListener('click', exportArchive);
    document.getElementById('banner-export').addEventListener('click', exportArchive);
    document.getElementById('banner-dismiss').addEventListener('click', hideBanner);
    document.getElementById('banner-restart').addEventListener('click', () => {
      if (!window.confirm(t('confirmRestart'))) return;
      circuitBroken = false;
      rejectedBlob = null;
      try { Module.FS.unlink(ARCHIVE_PATH); } catch (e) { /* 文件可能不存在 */ }
      api.load(ARCHIVE_PATH);
      hideBanner();
      persist();
      refresh();
    });

    document.getElementById('import-btn').addEventListener('click', () => {
      document.getElementById('import-file').click();
    });
    document.getElementById('import-file').addEventListener('change', (e) => {
      const file = e.target.files && e.target.files[0];
      if (file) importArchive(file);
      e.target.value = '';
    });

    // 表单联动
    ['f-id', 'f-name', 'f-date', 'f-train', 'f-board', 'f-alight', 'f-class']
      .forEach((id) => {
        const el = document.getElementById(id);
        el.addEventListener('input', refreshFormHints);
        el.addEventListener('change', refreshFormHints);
      });

    // 统计筛选
    ['s-date', 's-train', 's-class'].forEach((id) => {
      document.getElementById(id).addEventListener('change', renderStats);
    });

    // 标签页切换
    document.querySelectorAll('.tab').forEach((tab) => {
      tab.addEventListener('click', () => {
        document.querySelectorAll('.tab').forEach((x) => x.classList.remove('is-active'));
        document.querySelectorAll('.view').forEach((x) => x.classList.remove('is-active'));
        tab.classList.add('is-active');
        document.getElementById('view-' + tab.dataset.view).classList.add('is-active');
        if (tab.dataset.view === 'stats') renderStats();
      });
    });

    // 语言切换
    document.getElementById('lang-toggle').addEventListener('click', () => {
      lang = lang === 'zh' ? 'en' : 'zh';
      document.documentElement.lang = lang === 'zh' ? 'zh-CN' : 'en';
      document.getElementById('lang-toggle').textContent = lang === 'zh' ? 'English' : '中文';
      document.querySelectorAll('[data-i18n]').forEach((el) => {
        el.textContent = t(el.dataset.i18n);
      });
      buildSelects();
      refresh();
    });

    // 另一个标签页改了数据
    window.addEventListener('storage', (e) => {
      if (e.key === LS_ARCHIVE || e.key === LS_SEQ) setStatus(t('warnTabConflict'), true);
    });
  }

  boot().catch((err) => {
    console.error(err);
    setStatus('初始化失败：' + err.message, true);
  });
})();
