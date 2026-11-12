import { Map3D } from './map3d.js';
import { t, currentLang, setLang, translateDom, translateText } from './i18n.js';

const $ = (id) => document.getElementById(id);
const host = window.chrome?.webview?.hostObjects?.host;

/* ------------------------------------------------------------------ nav model */

const NAV = [
  { id: 'home', label: '主页', g: '◇' },
  {
    id: 'base', label: '基站', g: '◈',
    children: [
      
      { id: 'base.status', label: '基站状态' },
      
      { id: 'base.flash', label: '刷写固件' },
      
      { id: 'base.map', label: '位置查看' },
    ],
  },
  {
    id: 'track', label: '追踪', g: '◎',
    children: [
      { id: 'track.quality', label: '追踪质量' },
    ],
  },
  { id: 'driver', label: '驱动', g: '◉' },
  { id: 'log', label: '日志', g: '≡' },
  { id: 'about', label: '关于', g: '✦' },
];

let route = 'home';
const openGroups = { base: true, track: true };
let state = null;
let selected = null; // serial of the base shown in the detail panel
let flashChoice = null;
let flashArmed = false; // first click arms, second click writes
let restoreArmed = false;
let backupChoice = null;
let flashBusy = false; // a write or verify is running in the background
let busyLabel = '';   // which operation that is
let optical = null;    // last raw optical capture
// Whether the placement check is ours to stop. Kept here rather than derived from the
// backend flag: a click that lands while the capture is winding down used to restart it.
let optRunning = false;
let lastHtml = '';
let lastRoute = '';

/* ------------------------------------------------------------------ helpers */

const esc = (s) =>
  String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const logLines = [];

function log(text, cls = '') {
  logLines.push({ t: new Date().toTimeString().slice(0, 8), text, cls });
  if (logLines.length > 300) logLines.shift();
  const el = $('logbox');
  if (el) { paintLog(el); el.scrollTop = el.scrollHeight; }
}

function paintLog(el) {
  el.innerHTML = logLines
    .map((l) => `<div><span class="tm">${l.t}</span><span class="${l.cls}">${esc(l.text)}</span></div>`)
    .join('');
}

const driverName = { openhouse: 'openhouse', stock: '原厂未替换', missing: '文件缺失', unknown: '大小不符' };

// Both the flash page and the layout helper show a live log, and they used to share a single
// buffer — so a flash started on one page showed up on the other. Each page keeps its own
// lines now, and the backend tags every message with the operation that produced it.
const runLines = [];
const linkLines = [];
const drvLines = [];

function pushLine(lines, text, cls, repaint) {
  const stamp = new Date().toTimeString().slice(0, 8);
  // The waiting notice repeats every few seconds. Rewriting the last line in place keeps it
  // from burying everything that came before.
  const last = lines[lines.length - 1];
  if (cls === 'countdown' && last && last.countdown) {
    last.t = stamp;
    last.text = text;
    repaint();
    return;
  }
  lines.push({
    t: stamp,
    text,
    cls: cls === 'countdown' ? 'warn' : cls,
    countdown: cls === 'countdown',
  });
  if (lines.length > 300) lines.shift();
  repaint();
}

function runLog(text, cls = '') { pushLine(runLines, text, cls, paintRun); }
function linkLog(text, cls = '') { pushLine(linkLines, text, cls, paintLink); }
function drvLog(text, cls = '') { pushLine(drvLines, text, cls, paintDrv); }
function paintBusyBar() {
  // The top banner is gone. Flashing shows its progress in a modal, the placement check has
  // its own button state, so a page-wide bar was only duplicating them.
}
function paintLines(lines, id) {
  const el = $(id);
  if (!el) return;
  if (!lines.length) { el.innerHTML = '<div class="idle">操作记录会出现在这里。</div>'; return; }  el.innerHTML = lines
    .map((l) => `<div><span class="tm">${l.t}</span><span class="${l.cls}">${esc(l.text)}</span></div>`)
    .join('');
  el.scrollTop = el.scrollHeight;
}

function paintRun() { paintLines(runLines, 'runlog'); }
function paintLink() { paintLines(linkLines, 'linklog'); }
// Only the counters, in place. Rebuilding the page for them would reset scroll and repaint
// the whole card ten times a second.
function paintHits() {
  const devs = ((optical || {}).devices || []);
  for (let i = 0; i < devs.length; i++) {
    const slots = (devs[i].slots || []).slice().sort((a, b) => a.us - b.us);
    const devEl = $('htdev' + i);
    if (devEl && devEl.textContent !== devs[i].serial) devEl.textContent = devs[i].serial;
    for (let ch = 1; ch <= 4; ch++) {
      const el = $('hitch' + i + '_' + ch);
      if (!el) continue;
      const s = slots[ch - 1];
      const text = s ? String(s.hits) : '—';
      if (el.textContent !== text) el.textContent = text;
    }
  }
}

// Everything on the tracking page that changes with data, written in place so the page is
// never rebuilt under the user's hands. Called from the poll fast path and after a render.
function paintSlots() {
  const dev = ((optical || {}).devices || [])[0];
  const slots = dev ? dev.slots.slice().sort((a, b) => a.us - b.us) : [];
  const lit = slots.map((s) => s.hits).filter((h) => h > 0).sort((a, b) => a - b);
  const median = lit.length ? lit[Math.floor(lit.length / 2)] : 0;

  for (let ch = 1; ch <= 4; ch++) {
    const s = slots[ch - 1];

    const card = $('bsc' + ch);
    const stateEl = $('bst' + ch);
    let cls = 'bs miss';
    let text = '未检测到';
    if (s) {
      if (s.sweeps === 0) { cls = 'bs warn'; text = '收不到基站的光'; }
      else if (median > 0 && s.hits < median * 0.3) { cls = 'bs warn'; text = '光到不了 tracker'; }
      else { cls = ch === 1 ? 'bs ok master' : 'bs ok'; text = '正常'; }
    } else {
      // No slot for this channel: the card and the row already say so.
    }
    if (card && card.className !== cls) card.className = cls;
    const tt = translateText(text);
    if (stateEl && stateEl.textContent !== tt) stateEl.textContent = tt;

    const row = $('tqr' + ch);
    const usEl = $('tqu' + ch), seenEl = $('tqs' + ch), rotEl = $('tqo' + ch), stEl = $('tqx' + ch);
    if (!s) {
      if (row && row.className !== 'tq-row miss') row.className = 'tq-row miss';
      if (usEl) usEl.textContent = '—';
      if (seenEl) seenEl.textContent = '—';
      if (rotEl) rotEl.textContent = '—';
      if (stEl) stEl.textContent = translateText('没有信号');
      continue;
    }
    const seen = Math.round(s.seen);
    const k = seen >= 80 ? 'ok' : seen >= 20 ? 'mid' : 'low';
    const label = seen >= 80 ? '同步稳定' : seen >= 20 ? '间歇丢闪' : '偶尔出现';
    if (row && row.className !== 'tq-row ' + k) row.className = 'tq-row ' + k;
    if (usEl) usEl.textContent = s.us + ' µs';
    if (seenEl) seenEl.textContent = seen + '%';
    if (rotEl) rotEl.textContent = s.ratio || '—';
    const lbl = translateText(label);
    if (stEl && stEl.textContent !== lbl) stEl.textContent = lbl;
  }

  const tag = $('tq-tag');
  const tagText = translateText(optRunning ? '采集中' : (slots.length ? slots.length + ' 个时间槽' : '未检查'));
  if (tag && tag.textContent !== tagText) tag.textContent = tagText;
}
function paintDrv() { paintLines(drvLines, 'drvlog'); }

function tel(st) { return st?.telemetry || {}; }
function bases(st) { return tel(st).bases || []; }
function room(st) { return tel(st).room || {}; }

function crpDrives(st) { return (st?.drives || []).filter((d) => d.isCrp); }

/* ------------------------------------------------------------------ nav */

let lastNavHtml = null;

function renderNav() {
  const el = $('nav');
  const html = NAV.map((n) => {
    if (!n.children) {
      
      const on = route === n.id;
      
      return `<div class="item ${on ? 'on' : ''}" data-go="${n.id}">
      
        <span class="g">${n.g}</span><span class="lbl">${t(n.label)}</span></div>`;
    }
    const open = openGroups[n.id];
    const active = route.startsWith(n.id + '.');
    const kids = n.children
      
      .map((c) => `<div class="sub ${route === c.id ? 'on' : ''}" data-go="${c.id}">${t(c.label)}</div>`)
      
      .join('');
    return `<div class="item group ${active ? 'on' : ''}" data-toggle="${n.id}">
      
        <span class="g">${n.g}</span><span class="lbl">${t(n.label)}</span>
      
        <span class="caret ${open ? 'up' : ''}">⌄</span></div>
      
      <div class="subs ${open ? '' : 'hidden'}">${kids}</div>`;
  }).join('');
  // Rewriting the list under the pointer makes the browser drop the click: the node that
  // took mousedown is gone by mouseup, so no click is ever dispatched. On the placement page
  // the poll runs six times a second, which is exactly how the nav used to flicker and stop
  // responding there.
  if (html === lastNavHtml) return;
  lastNavHtml = html;
  el.innerHTML = html;
}

function navigate(id) {
  const leavingMap = route === 'base.map' && id !== 'base.map';
  route = id;
  flashArmed = false;
  restoreArmed = false;
  if (id.startsWith('base.')) openGroups.base = true;
  if (id.startsWith('track.')) openGroups.track = true;
  // The map page is the only one that needs the driver's 60 Hz stream, and the driver only
  // sends to connected clients: subscribe on entry, drop it on the way out.
  if (id === 'base.map') { try { host?.StartLive?.(); } catch { } }
  else if (leavingMap) { try { host?.StopLive?.(); } catch { } }
  renderNav();
  renderView();
}

/* ------------------------------------------------------------------ views */

function statusline(st) {
  const sv = st?.steamvr || {};
  const t = tel(st);
  const rs = room(st);
  if (!sv.found) return { k: 'bad', head: '没找到 SteamVR', sub: '先确认 SteamVR 装在哪儿，其余的都依赖它' };
  if (sv.driver !== 'openhouse') return { k: 'bad', head: '当前用的是原厂驱动', sub: '原厂驱动不认多站，最多只能跑两台' };
  if (crpDrives(st).length) return { k: 'warn', head: '有一台基站正等着刷写', sub: '它已经进了刷机模式，等你去刷固件' };
  if (!t.running) return { k: 'warn', head: '驱动在，但没有状态广播', sub: 'SteamVR 可能没启动，或者没跑到驱动这一步' };
  const tracked = (t.devices || []).filter((d) => d.valid).length;
  if (!tracked) return { k: 'warn', head: '基站都在，但没有设备在跟踪', sub: '打开 tracker 或手柄，房间才能继续收敛' };
  if (!rs.settled) return { k: 'warn', head: '房间还没收敛', sub: '有基站没定位好，看看是哪一台' };
  return { k: 'ok', head: '一切正常', sub: `房间已收敛，${bases(st).length} 台基站、${tracked} 个设备在工作` };
}

function renderHome(st) {
  const sv = st?.steamvr || {};
  const t = tel(st);
  const rs = room(st);
  const bs = bases(st);
  const crp = crpDrives(st);
  const s = statusline(st);

  const guides = [];
  if (!sv.found) guides.push(['没有定位到 SteamVR', '驱动页可以重新探测一次', 'driver']);
  if (sv.found && sv.driver !== 'openhouse') guides.push(['把驱动换成 openhouse', '原厂驱动只支持两台基站', 'driver']);
  if (sv.found && sv.driver === 'openhouse' && !t.running)
    guides.push(['启动 SteamVR', '驱动装好了，但没在跑，所以看不到基站', 'base.status']);
  if (crp.length) guides.push([`刷写 ${crp.length} 台待刷写的基站`, '接上后按页面步骤操作', 'base.flash']);
  if (t.running && !(t.devices || []).some((d) => d.valid))
    guides.push(['打开 tracker 或手柄', '光有基站不够，得有设备出光，房间才学得到东西', 'base.map']);
  if (t.running && !rs.settled) guides.push(['查明房间为什么不收敛', '基站状态页会标出没定位的那台', 'base.status']);
  if (t.running) guides.push(['看看基站摆在哪儿', '3D 视角看方位和朝向', 'base.map']);
  if (!guides.length) guides.push(['暂时没什么要做的', '系统在正常工作', null]);

  return `
  <div class="hero ${s.k}">
    <div class="hero-h">${esc(s.head)}</div>
    <div class="hero-s">${esc(s.sub)}</div>
  </div>

  ${lightBar(st)}

  <div class="grid3">
    <div class="card">
      
      <div class="card-h"><i class="lamp ${sv.found ? 'ok' : 'bad'}"></i>SteamVR</div>
      
      <div class="val">${sv.found ? '已找到' : '未找到'}</div>
      
      <div class="hint mono">${esc(sv.found ? sv.path : '—')}</div>
      
      <button class="b" data-go="driver">前往驱动</button>
    </div>
    <div class="card">
      
      <div class="card-h"><i class="lamp ${sv.driver === 'openhouse' ? 'ok' : sv.driver === 'stock' ? 'warn' : 'bad'}"></i>驱动</div>
      
      <div class="val" style="color:${sv.driver === 'openhouse' ? 'var(--ok)' : 'var(--warn)'}">${esc(driverName[sv.driver] || '—')}</div>
      
      <div class="hint">${sv.stockBackup ? '原厂备份在位' : '没有原厂备份'}${sv.vrserver ? ' · SteamVR 正在运行' : ''}</div>
      
      <button class="b" data-go="driver">前往驱动</button>
    </div>
    <div class="card">
      
      <div class="card-h"><i class="lamp ${t.running ? 'ok' : 'warn'}"></i>基站</div>
      
      <div class="val">${t.running ? bs.length + ' 台在线' : '未连接'}</div>
      
      <div class="hint">${t.running ? (rs.settled ? '房间已收敛' : '房间未收敛') + ` · 学习 ${rs.learned ?? 0} 台 · ${(t.devices || []).filter((d) => d.valid).length} 个设备` : '驱动遥测没有响应'}</div>
      
      <button class="b" data-go="base.status">前往基站</button>
    </div>
  </div>

  <div class="guide">
    <div class="card-h" style="margin-bottom:10px">接下来</div>
    ${guides
      
      .map(
      
        ([title, sub, go], i) => `
      
      <div class="g-row">
      
        <span class="g-idx mono">${i + 1}</span>
      
        <div class="g-txt"><b>${esc(title)}</b><span>${esc(sub)}</span></div>
      
        ${go ? `<button class="b" data-go="${go}">前往</button>` : '<span class="g-none">—</span>'}
      
      </div>`
      
      )
      
      .join('')}
  </div>`;
}

function baseState(b) {
  // Seen first: a station that was placed while a tracker was running stayed "已锁定" forever
  // after it was switched off, because none of the later states looked at whether its light
  // still reached us.
  if (!b.seen) return { t: '未在线', c: 'warn' };
  if (b.origin) return { t: '就绪', c: '' };
  if (b.locked) return { t: '已锁定', c: '' };
  if (b.placed) return { t: '未锁定', c: 'warn' };
  return { t: '未定位', c: 'warn' };
}

function baseRole(b) {
  if (b.origin) return '原点';
  if (!b.channel) return '—';
  return b.channel === '1' ? '主站' : '从站';
}

/* ------------------------------------------------------- diagnosis ladder */

// Which devices currently receive light from this station.
function seenBy(st, serial) {
  const out = [];
  for (const d of tel(st).devices || []) {
    if (!d.valid) continue;
    if ((d.coverage || []).some((c) => c.base === serial && c.frames > 0)) out.push(d.serial);
  }
  return out;
}

// The ladder a support person would walk: is it on, is it healthy, does anyone
// receive its light, and if so which stage of the solve is it stuck at.
function diagnoseBase(st, b) {
  const cov = seenBy(st, b.serial);
  const others = bases(st).filter((x) => x.serial !== b.serial);
  const othersCovered = others.some((x) => seenBy(st, x.serial).length > 0);
  const anyDevice = (tel(st).devices || []).some((d) => d.valid);

  if (!b.seen)
    return { n: 1, tone: 'bad', title: '没有收到这台基站',
      
      text: '驱动连它的红外广播都没听到。先看电源和指示灯，再确认频道设过、背面按键没卡住。' };

  if (b.faults > 0)
    return { n: 2, tone: 'bad', title: `这台报了 ${b.faults} 个故障`,
      
      text: '多半是激光功率上不去。已知的修法是：还原原厂固件 → 上电跑几秒 → 重新刷写。' };

  if (anyDevice && !cov.length && othersCovered)
    return { n: 3, tone: 'bad', title: '它在闪，但没有设备收到它的扫描',
      
      text: '其它基站能被设备看到，唯独这台没有：它很可能收不到主站的光学同步。检查它和主站之间有没有遮挡、正面是否对着主站。' };

  if (!b.placed)
    return { n: 3, tone: 'warn', title: '房间还没有它的位置',
      
      text: cov.length
      
        ? '有设备看到它，但视角不够。拿着 tracker 在那台基站前面晃动，多换几个位置。'
      
        : '没有任何设备看到它。拿着 tracker 到那台基站正前方 1～2 米，对着它挥动——它很可能根本不在这个基站的视野里。' };

  if (!b.learned)
    return { n: 4, tone: 'warn', title: '正在定位，还没学完',
      
      text: cov.length
      
        ? `已经有 ${cov.length} 个设备看到它，但帧数不够。对着那台基站来回晃动 tracker，让它持续处在视野里。`
      
        : '基站之间是通的，但 tracker 没被这台看到。拿着 tracker 走到那台基站正前方，对着它晃动，确认它在视野内。' };

  // locked is a stricter bar than learned (40+ keyframes, under 20 mm). A settled room
  // does not require it, so only flag it while the room is still converging.
  if (!b.locked && !b.origin && !room(st).settled)
    return { n: 4, tone: 'warn', title: '还在收敛，未锁定',
      
      text: `误差 ${(b.sigma * 1000).toFixed(0)} mm，要到 20 mm 以内。到那台基站正前方对着它晃动 tracker，静止和背对着都不算。` };

  return null;
}

// A strip, not a dialog: four lamps, one per stage, each carrying what to do about it.
// Bases are named by channel, because that is the only label the user can match to a
// physical unit.
function lightBar(st) {
  if (!tel(st).running) {
    return `<div class="diag idle"><span class="d-dot"></span>
      
      <div><b>没有状态广播</b><span>启动 SteamVR，基站状态才能读出来。</span></div></div>`;
  }

  const bs = bases(st);
  const problems = bs.map((b) => ({ b, d: diagnoseBase(st, b) })).filter((x) => x.d);
  const tgt = problems.length ? problems[0].b : null;
  const name = (b) => `频道 ${b.channel || '?'} 的基站`;
  const cov = tgt ? seenBy(st, tgt.serial) : [];
  const anyDevice = (tel(st).devices || []).some((d) => d.valid);
  const othersCovered = tgt
    ? bs.filter((x) => x.serial !== tgt.serial).some((x) => seenBy(st, x.serial).length > 0)
    : false;

  const steps = [
    {
      
      name: '已开启',
      
      ok: tgt ? tgt.seen : true,
      
      hint: tgt && !tgt.seen
      
        ? `没有收到${name(tgt)}。检查电源、背面指示灯和频道设置。`
      
        : '能收到它的红外广播',
    },
    {
      
      name: '无故障',
      
      ok: tgt ? tgt.faults === 0 : true,
      
      hint: tgt && tgt.faults > 0
      
        ? `${name(tgt)}报了 ${tgt.faults} 个故障，先还原原厂固件再刷回来。`
      
        : '没有硬件故障计数',
    },
    {
      
      name: '收到光照',
      
      ok: tgt ? cov.length > 0 : true,
      
      hint: !anyDevice
      
        ? '没有设备在线。先打开 tracker 或手柄。'
      
        : cov.length
      
          ? `${cov.length} 个设备能收到它的光`
      
          : !tgt
      
            
      ? '没有需要处理的基站'
      
            
      : othersCovered
      
            
        ? `${name(tgt)}在闪，但没有设备收到它的光。检查它和主站之间有没有遮挡、正面是否对着主站。`
      
            
        : `没有设备收到${name(tgt)}的光。把 tracker 拿到它的照射范围内，持续晃动数秒以定位。`,
    },
    {
      
      name: '锁定 tracker',
      
      ok: tgt ? tgt.locked || tgt.origin : true,
      
      hint: tgt && !tgt.locked && !tgt.origin
      
        ? `${name(tgt)}没有锁定 tracker，请拿着 tracker 在它的照射范围内晃动数秒，直至基站锁定 tracker。`
      
        : 'tracker 已被锁定',
    },
  ];

  const failed = steps.findIndex((s) => !s.ok);
  return `
  <div class="lights">
    ${steps
      
      .map((s, i) => {
      
        const cls = failed < 0 ? 'ok' : i < failed ? 'ok' : i === failed ? (i === 3 ? 'warn' : 'bad') : 'idle';
      
        return `<div class="light ${cls}">
      
          <span class="lamp"></span>
      
          <b>${i + 1} · ${s.name}</b>
      
          <span>${esc(cls === 'idle' ? '等上一步通过' : s.hint)}</span>
      
        </div>`;
      
      })
      
      .join('')}
  </div>`;
}

function detailHtml(b) {
  const s = baseState(b);
  return `
  <div class="detail">
    <div class="card-h">${esc(b.serial)} 详情</div>
    <div class="kv"><span>位置 x / y / z</span><b class="mono">${fmtVec(b.p)}</b></div>
    <div class="kv"><span>定位误差</span><b class="mono">${(b.sigma * 1000).toFixed(1)} mm</b></div>
    <div class="kv"><span>观测帧数</span><b class="mono">${b.observations}</b></div>
    <div class="kv"><span>故障计数</span><b class="mono" style="color:${b.faults ? 'var(--warn)' : 'inherit'}">${b.faults}</b></div>
    <div class="kv"><span>状态</span><b><span class="pill ${s.c}"><i></i>${s.t}</span></b></div>
  </div>`;
}

// The selection is drawn by a box that slides between rows. The rows themselves never
// change colour or weight: on Windows any change to what is behind the glyphs switches
// the text antialiasing, which is what made the type look like it changed.
function placeSelbox(animate) {
  const box = $('selbox');
  const row = document.querySelector(`.r[data-base="${selected}"]`);
  if (!box || !row) return;
  if (!animate) box.classList.add('instant');
  box.style.height = `${row.offsetHeight - 2}px`;
  box.style.transform = `translateY(${row.offsetTop + 1}px)`;
  box.classList.add('on');
  if (!animate) {
    void box.offsetHeight; // commit the jump before re-enabling transitions
    box.classList.remove('instant');
  }
}

function renderBaseStatus(st) {
  const t = tel(st);
  const raw = bases(st);
  if (!t.running) return emptyState('驱动没有在广播状态', 'SteamVR 没启动，或者驱动不是 openhouse 的', 'base.flash');

  if (!raw.length) return emptyState('房间里还没有基站', '上电、设好频道之后会自己出现', 'base.flash');

  // Ordered by channel, because that is what the user set on the device itself.
  const bs = raw.slice().sort((a, b) => {
    const na = parseInt(a.channel, 10);
    const nb = parseInt(b.channel, 10);
    return (Number.isNaN(na) ? 99 : na) - (Number.isNaN(nb) ? 99 : nb);
  });

  if (!selected || !bs.some((b) => b.serial === selected)) selected = bs[0].serial;
  const sel = bs.find((b) => b.serial === selected);
  const stt = baseState(sel);

  return `
  <div class="h"><h2>基站状态</h2><span class="tag">${bs.length} 台</span></div>
  <p class="sub-line">${t.room?.settled ? '房间已收敛' : '房间未收敛'} · 学习 ${t.room?.learned ?? 0} 台 · 看到 ${t.room?.seen ?? 0} 台</p>

  ${lightBar(st)}

  <div class="tbl" id="basetbl">
    <div class="r hd"><span>频道</span><span>序列号</span><span>角色</span><span>状态</span></div>
    <div class="selbox" id="selbox"></div>
    ${bs
      
      .map((b) => {
      
        const s = baseState(b);
      
        return `<div class="r click" data-base="${esc(b.serial)}">
      
          <span class="ch">${esc(b.channel || '—')}</span>
      
          <span class="id">${esc(b.serial)}</span>
      
          <span class="role">${baseRole(b)}</span>
      
          <span><span class="pill ${s.c}"><i></i>${s.t}</span></span>
      
        </div>`;
      
      })
      
      .join('')}
  </div>

  ${detailHtml(sel)}`;
}

function fmtVec(v) {
  if (!v || !v.length) return '—';
  return v.map((n) => (n >= 0 ? ' ' : '') + n.toFixed(3)).join('  ');
}

function emptyState(title, sub, go) {
  return `<div class="empty">
    <div class="empty-h">${esc(title)}</div>
    <div class="empty-s">${esc(sub)}</div>
    ${go ? `<button class="b" data-go="${go}">前往刷写固件</button>` : ''}
  </div>`;
}

function renderFlash(st) {
  const crp = st.crp || {};
  const hasCrp = !!crp.letter;
  const fw = st.firmware || [];
  const stock = st.stock || { ok: false };
  if (!flashChoice || !fw.some((f) => f.name === flashChoice)) flashChoice = fw[0]?.name || null;
  const sel = fw.find((f) => f.name === flashChoice);
  // What the station in ISP mode is running, read from its application area.
  const kind = crp.kind || 'unknown';
  const kindText = kind === 'patch' ? '破解固件' : kind === 'stock' ? '原厂固件' : '—';

  // Nothing on this page works until a factory image is archived. Without one a flash is a
  // one-way door, so the gate is closed before anyone walks through it, not after.
  if (!stock.ok) return renderStockGate(st, crp, hasCrp);

  // The tag describes this page only. Another page's operation may be running in the
  // background, and showing its name here would read as if this page had started it.
  const fwBusy = flashBusy && busyLabel.indexOf('固件') >= 0;

  return `
  <div class="h"><h2>刷写固件</h2><span class="tag${fwBusy ? ' busy' : ''}">${fwBusy ? '操作进行中' : hasCrp ? '发现设备' : '等待设备'}</span></div>
  <p class="sub-line">一次只接一台。写入只覆盖前 49,664 字节，序列号和激光标定存在别的芯片里，不会被碰。</p>

  <div class="steps">
    <div class="step ${hasCrp ? 'done' : 'now'}"><span class="n mono">1</span><div><b>断电</b><span>拔掉基站电源</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">2</span><div><b>按住背面按钮</b><span>按住不要松</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">3</span><div><b>插 micro-USB</b><span>必须是数据线</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">4</span><div><b>插电源，再松手</b><span>出现 CRP DISABLED 盘</span></div></div>
  </div>

  <div class="cols">
    <div class="card">
      
      <div class="sec">当前基站</div>
      
      <div class="detect ${hasCrp ? 'found' : ''}">
      
        <span class="dot"></span>
      
        <b>${hasCrp ? '已连接 ' + esc(crp.letter) : '没有检测到设备'}</b>
      
        <span class="mono">${hasCrp ? esc(crp.label) : '按上面四步接上'}</span>
      
      </div>
      
      <div class="kv"><span>SHA256</span><b class="mono small">${hasCrp && crp.sha256 ? esc(crp.sha256.slice(0, 32)) : '—'}</b></div>
      
      <div class="kv"><span>状态</span><b class="${kind === 'patch' ? 'warn' : ''}">${hasCrp ? kindText : '—'}</b></div>
    </div>

    <div class="card">
      
      <div class="sec">${restoreArmed ? '还原到原始固件' : '固件镜像'}</div>
      
      ${restoreArmed
      
        ? `<div class="kv"><span>固件</span><b class="mono">${esc(stock.file || '')}</b></div>
      
           <div class="kv"><span>SHA256</span><b class="mono small">${esc(stock.sha || '—')}</b></div>
      
           <div class="kv"><span>备份于</span><b class="mono">${esc(stock.date || '—')}</b></div>
      
           <div class="kv"><span>大小</span><b class="mono">${((stock.size || 0) / 1024).toFixed(0)} KB</b></div>`
      
        : `<select id="fw" class="sel" ${fw.length ? '' : 'disabled'}>
      
            
       ${fw.length ? fw.map((f) => `<option ${f.name === flashChoice ? 'selected' : ''}>${esc(f.name)}</option>`).join('') : '<option>没有找到固件镜像</option>'}
      
           </select>
      
           <div class="kv"><span>大小</span><b class="mono">${sel ? (sel.size / 1024).toFixed(0) + ' KB' : '—'}</b></div>
      
           <div class="kv"><span>SHA256</span><b class="mono small">${sel && sel.sha256 ? esc(sel.sha256.slice(0, 32)) : '—'}</b></div>
      
           <div class="kv"><span>日期</span><b class="mono">${sel ? esc(sel.date) : '—'}</b></div>`}
    </div>
  </div>

  <div class="acts">
    <button class="b" id="btn-verify" ${hasCrp && !flashBusy && !restoreArmed ? '' : 'disabled'}>只读检查</button>
    <button class="b" id="btn-restore" ${hasCrp && !flashBusy ? '' : 'disabled'} title="写回备份的原始固件">${restoreArmed ? '取消还原' : '还原原厂固件'}</button>
    <span class="grow"></span>
    ${flashBusy
      
      ? '<button class="b p" disabled>操作进行中…</button>'
      
      : restoreArmed
      
        ? `<button class="b p armed" id="btn-do-restore" ${hasCrp ? '' : 'disabled'}>确认还原</button>`
      
        : `<button class="b p ${flashArmed ? 'armed' : ''}" id="btn-flash" ${hasCrp && sel ? '' : 'disabled'}>${flashArmed ? '确认写入' : '刷写破解固件'}</button>`}
  </div>
  ${fwBusy ? '<p class="note">后台在等设备：拔掉 USB 和电源、按原步骤重新插一次，它才会自己完成校验。</p>' : ''}

  <div class="runlog" id="runlog"></div>
  <p class="note">写入后需要拔线重插一次才能完成校验——只有重插，读回来的才是 flash 里的真实内容。</p>`;
}

// Shown in place of the write controls while no factory image is archived. Deliberately not a
// greyed-out button with a tooltip: the reason has to be readable without hovering anything.
function renderStockGate(st, crp, hasCrp) {
  return `
  <div class="h"><h2>刷写固件</h2><span class="tag warn">先备份原始固件</span></div>
  <p class="sub-line">备份一份原始固件之后才能刷写。同一份可以给所有基站用。</p>

  <div class="steps">
    <div class="step ${hasCrp ? 'done' : 'now'}"><span class="n mono">1</span><div><b>断电</b><span>拔掉基站电源</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">2</span><div><b>按住背面按钮</b><span>按住不要松</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">3</span><div><b>插 micro-USB</b><span>必须是数据线</span></div></div>
    <div class="step ${hasCrp ? 'done' : ''}"><span class="n mono">4</span><div><b>插电源，再松手</b><span>出现 CRP DISABLED 盘</span></div></div>
  </div>

  <div class="gate">
    <div class="gate-mark"></div>
    <div>
      <b>还没有原始固件备份</b>
      <p>找一台<b>还没刷过破解固件</b>的基站，按上面四步接上，再点下面的按钮。</p>
      <p class="dim">手头没有这样的基站，也会从之前的备份里找一份。</p>
    </div>
  </div>

  <div class="detect ${hasCrp ? 'found' : ''}">
    <span class="dot"></span>
    <b>${hasCrp ? '已连接 ' + esc(crp.letter) : '没有检测到设备'}</b>
    <span class="mono">${hasCrp ? esc(crp.label) : '接上设备再点备份'}</span>
  </div>

  <div class="acts">
    <span class="grow"></span>
    <button class="b p" id="btn-stock">备份原始固件</button>
  </div>

  <div class="runlog" id="runlog"></div>`;
}
// The table under the 3D view. Every placed station gets one row: where it is, how well
// it is pinned down, and how many frames went into that estimate. The 3D scene answers
// "which direction", this answers "how sure".
function mapDataHtml(st) {
  const bs = bases(st).filter((b) => b.p && b.p.length === 3);
  if (!bs.length) return '';

  const head = `<div class="mtr mth">
    <span>频道</span><span>序列号</span><span>误差</span><span>解算帧数</span>
    <span>x</span><span>y</span><span>z</span><span>状态</span>
  </div>`;

  const rows = bs.map((b) => {
    const [x, y, z] = b.p;
    const mm = Number.isFinite(b.sigma) ? b.sigma * 1000 : NaN;
    const mmTxt = Number.isFinite(mm) ? (mm < 10 ? mm.toFixed(1) : mm.toFixed(0)) : '—';
    // 15 mm is where a station's placement stops being trustworthy for a room solve.
    const bad = !Number.isFinite(mm) || mm > 15;
    const s = baseState(b);
    return `<div class="mtr">
      <span class="c-ch">${esc(String(b.channel ?? '—'))}</span>
      <span class="c-id">${esc(b.serial || '')}</span>
      <span class="c-n${bad ? ' bad' : ''}">${mmTxt}</span>
      <span class="c-n">${b.observations ?? 0}</span>
      <span class="c-n">${x.toFixed(3)}</span>
      <span class="c-n">${y.toFixed(3)}</span>
      <span class="c-n">${z.toFixed(3)}</span>
      <span class="c-st"><span class="pill ${s.c}"><i></i>${s.t}</span></span>
    </div>`;
  }).join('');

  return `<div class="mtable">${head}${rows}</div>`;
}
function renderMap(st) {
  const t = tel(st);
  const bs = (t.bases || []).filter((b) => b.p && b.p.length === 3);
  if (!t.running) return emptyState('驱动没有在广播状态', 'SteamVR 没启动，或者驱动不是 openhouse 的', 'base.flash');
  if (!bs.length) return emptyState('没有可画的基站', '上电、设好频道之后会自己出现', 'base.flash');

  const devs = (t.devices || []).filter((d) => d.valid);
  return `
  <div class="h"><h2>位置查看</h2><span class="tag" id="map-tag">${bs.length} 台 · ${devs.length} 个设备</span></div>
  <p class="sub-line">拖动旋转，滚轮缩放。锥体是解算出的照射方向。原点基站位姿按定义为零，永远在正中间。</p>
  <div class="map3d" id="map3d"></div>
  <div class="mapdata" id="mapdata">${mapDataHtml(st)}</div>`;
}
function renderDriver(st) {
  const sv = st.steamvr || {};
  const running = sv.running || [];
  const state = sv.driver === 'openhouse' ? 'openhouse 在位'
    : sv.driver === 'stock' ? '原厂驱动'
    : sv.driver === 'missing' ? '文件缺失' : '未知';

  // Same rule as the flash page: a background flash must not relabel the driver page.
  const drvBusy = flashBusy && busyLabel.indexOf('驱动') >= 0;
  return `
  <div class="h"><h2>驱动</h2><span class="tag${drvBusy ? ' busy' : ''}">${drvBusy ? '操作进行中' : state}</span></div>
  <p class="sub-line">把 openhouse 驱动装进 SteamVR。安装会先结束 Steam 进程，并把 Valve 的原驱动保留成 .stock。</p>

  <div class="cols">
    <div class="card">
      
      <div class="sec">SteamVR</div>
      
      <div class="detect ${sv.found ? 'found' : ''}">
      
        <span class="dot"></span>
      
        <b>${sv.found ? '已找到' : '未找到'}</b>
      
        <span class="mono">${esc(sv.path || '—')}</span>
      
      </div>
      
      <div class="kv"><span>占用文件的进程</span><b>${running.length ? esc(running.join('  ')) : '没有'}</b></div>
    </div>

    <div class="card">
      
      <div class="sec">驱动文件</div>
      
      <div class="kv"><span>当前</span><b class="${sv.driver === 'openhouse' ? '' : 'warn'}">${state}</b></div>
      
      <div class="kv"><span>大小</span><b class="mono">${sv.driverSize ? (sv.driverSize / 1048576).toFixed(2) + ' MB' : '—'}</b></div>
      
      <div class="kv"><span>原厂备份</span><b>${sv.stockBackup ? '有' : '没有'}</b></div>
    </div>
  </div>

  <div class="acts">
    <button class="b p" id="btn-drv-install" ${sv.found && !flashBusy ? '' : 'disabled'}>${sv.driver === 'openhouse' ? '重新安装' : '安装驱动'}</button>
    <button class="b" id="btn-drv-uninstall" ${sv.found && sv.stockBackup && !flashBusy ? '' : 'disabled'}>还原原厂驱动</button>
    <span class="grow"></span>
  </div>
  ${drvBusy ? '<p class="note">正在执行，完成后状态会自动刷新。</p>' : ''}

  <div class="runlog" id="drvlog"></div>`;
}

function renderTrackQuality(st) {
  // Structure only: the cards, the four rows and the warning box are fixed, and paintSlots
  // fills them in place. Keeping the numbers out of the markup means a poll that only
  // changed a counter never rebuilds the page, which used to throw away the scroll position
  // a moment after the user dragged it.
  const checking = optRunning;

  const stations = [1, 2, 3, 4].map((ch) =>
    `<div class="bs miss" id="bsc${ch}">
      <div class="bs-icon"><span class="lens"></span></div>
      <div class="bs-ch">频道 ${ch}</div>
      ${ch === 1 ? '<div class="bs-tag">主基站</div>' : ''}
      <div class="bs-state" id="bst${ch}">未检测到</div>
    </div>`).join('');

  const rows = [1, 2, 3, 4].map((ch) =>
    `<div class="tq-row miss" id="tqr${ch}">
      <span class="tq-ch">${ch}</span>
      <span class="tq-n" id="tqu${ch}">—</span>
      <span class="tq-n" id="tqs${ch}">—</span>
      <span class="tq-n" id="tqo${ch}">—</span>
      <span class="tq-st" id="tqx${ch}">没有信号</span>
    </div>`).join('');

  // One row per receiver: with more than one tracker connected the old two-column list could
  // only show the first one.
  const devs = (optical || {}).devices || [];
  const hitHead = '<div class="ht-row ht-head"><span class="ht-dev">设备</span>' +
    '<span class="ht-n">频道 1</span><span class="ht-n">频道 2</span><span class="ht-n">频道 3</span><span class="ht-n">频道 4</span></div>';
  const hitRows = devs.length
    ? devs.map((d, i) => `<div class="ht-row">
        <span class="ht-dev" id="htdev${i}">${esc(d.serial)}</span>
        <span class="ht-n" id="hitch${i}_1">—</span>
        <span class="ht-n" id="hitch${i}_2">—</span>
        <span class="ht-n" id="hitch${i}_3">—</span>
        <span class="ht-n" id="hitch${i}_4">—</span>
      </div>`).join('')
    : `<div class="ht-row"><span class="ht-dev">未开始检测</span>
        <span class="ht-n">—</span><span class="ht-n">—</span><span class="ht-n">—</span><span class="ht-n">—</span></div>`;

  return `
  <div class="tq-page">
  <div class="h" style="margin-bottom:16px"><h2>追踪质量</h2><span class="tag${checking ? ' busy' : ''}" id="tq-tag">${checking ? '采集中' : '未检查'}</span></div>

  <div class="diag warn diag-center" style="margin-bottom:14px">
    <span class="d-dot"></span>
    <div>
      <b>点击“开始检查”会自动关闭 SteamVR 相关进程</b>
    </div>
  </div>

  <div class="acts" style="margin-top:0">
    <button class="b ${checking ? 'stop' : 'p'}" id="btn-link">${checking ? '停止检查' : '开始检查'}</button>
    <span class="grow"></span>
  </div>
  <p class="note spaced">检查会一直进行，数据实时刷新，直到你点停止或者满 10 分钟。<br>可以在各个基站能照射到的范围内晃动 tracker 以查看追踪质量。</p>

  <div class="bsgrid" style="margin-bottom:14px">${stations}</div>
  <div class="tq">
    <div class="tq-head"><span class="tq-ch">序号</span><span class="tq-n">时间偏移</span><span class="tq-n">出现比例</span><span class="tq-n">轮转</span><span class="tq-st">同步状态</span></div>
    ${rows}
  </div>
  <div class="hits" style="margin-top:14px">
    <div class="card-h">激光击中次数</div>
    <div class="hits-table">${hitHead}${hitRows}</div>
  </div>
  </div>`;
}

function renderAbout() {
  return `
  <div class="h"><h2>关于</h2><span class="tag">openhouse setup</span></div>
  <p class="sub-line">给 HTC Vive 一代基站的图形化工具：刷固件、装驱动、看摆位。</p>

  <div class="cols">
    <div class="card">
      <div class="sec">贡献</div>
      <div class="kv"><span>原作者</span><b>YusufOzmen01 / openhouse</b></div>
      <div class="kv"><span>源码修改和 UI 设计</span><b>Bilibili：RagdollUser<br>和他可爱的 DeepSeek 白白子</b></div>
    </div>
    <div class="card">
      <div class="sec">联系与源码</div>
      <div class="kv"><span>QQ</span><b class="mono">1746723295</b></div>
      <div class="kv"><span>仓库</span><b class="mono small"><a href="https://github.com/lindaobie/openhouse_with_GUI" data-open="https://github.com/lindaobie/openhouse_with_GUI">github.com/lindaobie/openhouse_with_GUI</a></b></div>
      <div class="kv"><span>上游</span><b class="mono small"><a href="https://github.com/YusufOzmen01/openhouse" data-open="https://github.com/YusufOzmen01/openhouse">github.com/YusufOzmen01/openhouse</a></b></div>
    </div>
  </div>

  <div class="card" style="margin-top:14px">
    <div class="sec">实现原理</div>
    <p class="note" style="margin-top:0">
      <b>基站为什么能超过两台。</b>一代基站的扫描是时分复用：同一时刻只有一台在扫。改过的固件让 2～4 台各自占一个时间槽轮班，每台扫 1/N 的时间。原厂固件只支持两台，因为它只用了两个槽。
    </p>
    <p class="note">
      <b>驱动做了什么。</b>openhouse 驱动接管 SteamVR 的 lighthouse 驱动：读每个传感器的光电脉冲、解算出位姿，再按 SteamVR 的接口交出去。基站自己的位置和朝向也在这个过程中被解算出来，位置很准，朝向依赖角度的多样性。
    </p>
    <p class="note">
      <b>刷写为什么只写 48 KB。</b>基站在 ISP 模式下会挂出一个 128 KB 的盘，那是整片 flash。我们的镜像只有 49,408 字节，是应用程序；写入时只覆盖开头，后面 81 KB 一个字节不碰——那里从 0x1F000 开始有 EEPROM 区，存着每台的序列号和激光标定。所以刷写不会把标定刷丢，也不能拿别人的备份整片写回去。
    </p>
    <p class="note">
      <b>摆位助手为什么不用驱动。</b>它直接读接收器的原始光脉冲，数的是"每秒有多少次激光扫到 tracker"。绕开驱动的好处是：驱动不工作时它照样能看，调试时这是唯一还看得见的眼睛。代价是接收器同一时间只能被一个程序占用，所以检查前得先把 SteamVR 退掉。
    </p>
    <p class="note">
      <b>这个工具本身。</b>刷写、驱动安装、摆位检查原本是几份 PowerShell 脚本，散在各个目录里。界面把它们收进来，但不重写其中已验证的逻辑：镜像向量校验、读保护检查、512 字节对齐原地写、先备份再写、强制断电重插后才校验，都是脚本里踩出来的规矩。
    </p>
  </div>`;
}
function renderSimple(title, body) {
  return `<div class="h"><h2>${esc(title)}</h2><span class="tag">本版未改动</span></div>
      
          <p class="sub">${esc(body)}</p>`;
}

function renderView() {
  const st = state;
  const view = $('view');
  // Pages that own their own scroll containers: their columns fill the window and scroll
  // internally, so nothing the busy bar does can push them over the edge.
  view.classList.toggle('fit', route === 'base.map' || route === 'track.quality' || route === 'base.flash');
  if (!st) {
    view.innerHTML = `<div class="empty"><div class="empty-h">正在读取…</div></div>`;
    return;
  }
  let html = '';
  // On the 3D page the poll must not rebuild the DOM: a new container means a new scene,
  // and the camera would jump back to its default on every refresh.
  if (route === 'base.map' && view.querySelector('#map3d')) {
    const tag = $('map-tag');
    const t = tel(st);
    if (tag) {
      
      const dv = (t.devices || []).filter((d) => d.valid).length;
      
      tag.textContent = `${(t.bases || []).length} 台 · ${dv} 个设备`;
    }
    const md = $('mapdata');
    if (md) md.innerHTML = mapDataHtml(st);
    Map3D.show(view.querySelector('#map3d'), st);
    return;
  }
  const safe = (fn) => {
    try { return fn(); } catch (e) {
      
      return `<div class="empty"><div class="empty-h">渲染失败</div>
      
        <div class="empty-s">${esc(e && e.message ? e.message : e)}</div></div>`;
    }
  };
  switch (route) {
    case 'home': html = safe(() => renderHome(st)); break;
    case 'base.status': html = safe(() => renderBaseStatus(st)); break;
    case 'base.flash': html = safe(() => renderFlash(st)); break;
    case 'base.map': html = safe(() => renderMap(st)); break;
    case 'track.quality': html = safe(() => renderTrackQuality(st)); break;
    case 'driver': html = safe(() => renderDriver(st)); break;
    case 'about': html = safe(() => renderAbout()); break;
    case 'log': html = `<div class="h"><h2>日志</h2><span class="tag" id="log-count">${logLines.length} 条</span></div>
      <p class="sub-line">每次启动会往软件目录下的 logs 文件夹写一个文本文件，保留最近 20 次。</p>
      <div class="logpath"><span class="mono">${esc(st.logdir || '日志目录不可写')}</span></div>
      <div class="acts" style="margin-top:10px"><button class="b" id="btn-log-copy">复制全部</button><button class="b" id="btn-log-clear">清空</button><button class="b" id="btn-log-open" ${st.logdir ? '' : 'disabled'}>打开日志目录</button><span class="grow"></span></div>
      <div class="log log-full" id="logbox"></div>`; break;
    default: html = renderSimple('未知页面', route);
  }
  const keepLog = $('logbox');
  void keepLog;
  // Rebuilding the view on every poll throws away what the user is doing: a select closes,
  // focus is lost, the page scroll jumps back. Only touch the DOM when the markup itself
  // changed.
  if (html === lastHtml && route === lastRoute) {
    if (route === 'log') { const box = $('logbox'); if (box) { paintLog(box); box.scrollTop = box.scrollHeight; } }
    if (route === 'base.status') placeSelbox(false);
    if (route === 'base.flash') paintRun();
    if (route === 'driver') paintDrv();
    if (route === 'track.quality') { paintHits(); paintSlots(); paintLink(); }
    translateDom(view);
    return;
  }
  const keepScroll = view.scrollTop;
  try {
    view.innerHTML = html;
    lastHtml = html;
    lastRoute = route;
    view.scrollTop = keepScroll;
  } catch (err) {
    view.innerHTML = `<div class="empty"><div class="empty-h">渲染失败</div>
      
      <div class="empty-s">${esc(err && err.message ? err.message : err)}</div></div>`;
    return;
  }
  if (route === 'log') {
    const box = $('logbox');
    if (box) { paintLog(box); box.scrollTop = box.scrollHeight; }
  }
  if (route === 'base.status') placeSelbox(false);
  if (route === 'base.flash' || route === 'driver') {
    if (route === 'driver') paintDrv(); else paintRun();
  }
  if (route === 'track.quality') { paintHits(); paintSlots(); paintLink(); }
  if (route === 'base.map') { const c = $('map3d'); if (c) Map3D.show(c, st); } else { Map3D.hide(); }
  // Last, so the counters painted above are translated too.
  translateDom(view);
}

/* ------------------------------------------------------------------ polling */

async function refresh(silent = true) {
  if (!host) { log('宿主桥接没起来', 'warn'); return; }
  try {
    state = JSON.parse(await host.GetStatus());
    renderNav();
    if (!silent) {
      
      const sc = $('side-foot');
      
      const sv = state.steamvr || {};
      
      if (sc) sc.innerHTML = sv.found
      
        ? `SteamVR<br><span class="mono" style="color:#7e838d">${esc(driverName[sv.driver] || '')}</span>`
      
        : '没有找到 SteamVR';
      
      log('已刷新', 'ok');
    }
    renderView();
  } catch (e) {
    log(`读取失败：${e?.message || e}`, 'warn');
  }
}

document.addEventListener('change', (e) => {
  if (!e.target) return;
  if (e.target.id === 'fw') flashChoice = e.target.value;
  if (e.target.id === 'bk') backupChoice = e.target.value;
});

document.addEventListener('click', async (e) => {
  const ol = e.target.closest('[data-open]');
  if (ol) {
    e.preventDefault();
    host?.OpenUrl(ol.dataset.open);
    return;
  }
  const go = e.target.closest('[data-go]');
  if (go) { navigate(go.dataset.go); return; }
  const tg = e.target.closest('[data-toggle]');
  if (tg) {
    const id = tg.dataset.toggle;
    openGroups[id] = !openGroups[id];
    renderNav();
    return;
  }
  const lc = e.target.closest('#btn-log-clear');  if (lc) {
    logLines.length = 0;
    const box = $('logbox');
    if (box) paintLog(box);
    const tag = $('log-count');
    if (tag) tag.textContent = '0 条';
    return;
  }

  const lo = e.target.closest('#btn-log-open');
  if (lo) {
    if (host) host.OpenLogDir();
    return;
  }

  const lcp = e.target.closest('#btn-log-copy');
  if (lcp) {
    const text = logLines.map((l) => l.t + ' ' + l.text).join('\n');
    try { await navigator.clipboard.writeText(text); log('已复制 ' + logLines.length + ' 条日志', 'ok'); }
    catch { runLog('复制失败，浏览器不允许访问剪贴板', 'warn'); }
    return;
  }

  const lk = e.target.closest('#btn-link');
  if (lk) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    if (optRunning) {
      optRunning = false;
      linkLog('停止检查…');
      try { await host.StopOptical(); } catch (err) { log('停止失败：' + (err?.message || err), 'warn'); }
      renderView();
      return;
    }
    if (flashBusy) { showBusyNotice(busyLabel); return; }
    optRunning = true;
    linkLines.length = 0;
    optical = null;
    linkLog('开始检查追踪质量…');
    const err = await host.StartOptical(600);
    if (err) { optRunning = false; linkLog(err, 'warn'); }
    renderView();
    return;
  }

  const di = e.target.closest('#btn-drv-install');
  if (di) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    drvLines.length = 0;
    drvLog('安装驱动：会先结束 Steam 进程');
    const err = await host.StartDriver('install');
    if (err) drvLog(err, 'warn');
    return;
  }

  const du = e.target.closest('#btn-drv-uninstall');
  if (du) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    drvLines.length = 0;
    drvLog('还原 SteamVR 原驱动');
    const err = await host.StartDriver('uninstall');
    if (err) drvLog(err, 'warn');
    return;
  }

  const v = e.target.closest('#btn-verify');
  if (v) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    runLog('只读检查…');
    const err = await host.StartFlash('verify', flashChoice || '');
    if (err) runLog(err, 'warn');
    return;
  }

  const sc = e.target.closest('#btn-stock');
  if (sc) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    runLines.length = 0;
    runLog('正在找原始固件…');
    const err = await host.ExtractStock();
    if (err) runLog(err, 'warn');
    refresh();
    return;
  }

  const rs = e.target.closest('#btn-restore');
  if (rs) {
    restoreArmed = !restoreArmed;
    flashArmed = false;
    renderView();
    if (restoreArmed) runLog('确认后会把备份的原始固件写回去。', 'warn');
    return;
  }

  const dr = e.target.closest('#btn-do-restore');
  if (dr) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    runLog('还原：备份的原始固件');
    const err = await host.StartFlash('restore', '');
    if (err) runLog(err, 'warn');
    restoreArmed = false;
    renderView();
    return;
  }

  const fl = e.target.closest('#btn-flash');
  if (fl) {
    if (!host) { runLog('宿主桥接没起来', 'warn'); return; }
    if (!flashArmed) {
      
      flashArmed = true;
      
      renderView();
      
      runLog('再点一次确认写入。', 'warn');
      
      return;
    }
    flashArmed = false;
    renderView();
    runLog('开始刷写…');
    const err = await host.StartFlash('flash', flashChoice || '');
    if (err) runLog(err, 'warn');
    return;
  }

  const br = e.target.closest('[data-base]');
  if (br) {
    if (br.dataset.base === selected) return;
    selected = br.dataset.base;
    const b = (state?.telemetry?.bases || []).find((x) => x.serial === selected);
    const old = document.querySelector('.detail');
    if (b && old) {
      
      old.outerHTML = detailHtml(b);
      
      document.querySelector('.detail')?.classList.add('swap');
    }
    placeSelbox(true);
  }
});

// The modal is shared by the wait gate and the blocked-action notice. Each of them restores
// every field it uses, so state from one can never bleed into another.
let modalTimer = null;


// A blocked click should say why it did nothing. Staying silent reads as a broken button.
function setTxt2(text) {
  const el = $('modal-txt2');
  el.innerHTML = text || '';
  el.style.display = text ? '' : 'none';
}

function showBusyNotice(label) {
  const op = label || '操作';
  clearInterval(modalTimer);
  $('modal-badge').className = 'modal-badge';
  $('modal-badge').textContent = '!';
  $('modal-h').textContent = `${op}还没结束`;
  $('modal-stamp').style.display = 'none';
  $('modal-txt').textContent = `${op}正在进行，请完成当前的${op}操作，再进行其他操作。`;
  setTxt2(op.indexOf('固件') >= 0 ? '等基站重新接上、校验通过之后，刷写页才会解除锁定。' : '');
  const btn = $('modal-ok');
  btn.disabled = false;
  btn.textContent = '知道了';
  $('overlay').hidden = false;
}

// Shown the moment a write finishes, and kept on screen until the station is back on the bus.
// Steps only — the reason the cable has to come out is in the docs, not in the way.
let waitOpen = false;
let waitDone = false;
function showWaitNotice(step) {
  clearInterval(modalTimer);
  waitDone = false;
  $('modal-badge').className = 'modal-badge';
  $('modal-badge').textContent = '!';
  $('modal-h').textContent = step ? '正在等基站重新接上' : '固件已写入，还差一步';
  $('modal-stamp').style.display = 'none';
  $('modal-txt').innerHTML = step
    ? '请按照操作重新连接基站，以验证固件完整性。<div class="modal-steps">① 按住背面按钮，不要松<br>② 插 micro-USB<br>③ 插电源，然后松手</div>'
    : '现在把基站上的线全部拔掉，然后重新接一次：<div class="modal-steps">① 拔掉 USB 和电源<br>② 按住背面按钮，不要松<br>③ 插 micro-USB<br>④ 插电源，然后松手</div>';
  setTxt2('');
  const btn = $('modal-ok');
  // No manual close: the whole point is that this step must be finished before anything
  // else can happen. The button is replaced by a status line until the station is back.
  btn.disabled = true;
  btn.textContent = '等待基站重新接上…';
  $('overlay').hidden = false;
  waitOpen = true;
}

function hideWait() {
  if (!waitOpen) return;
  waitOpen = false;
  waitDone = false;
  $('overlay').hidden = true;
}

// Verification finished: the dialog stops being a gate and becomes a result to acknowledge.
function showWaitResult(kind) {
  clearInterval(modalTimer);
  waitDone = true;
  $('modal-badge').className = 'modal-badge' + (kind === 'ok' ? ' ok' : '');
  $('modal-badge').textContent = kind === 'ok' ? '✓' : '!';
  $('modal-stamp').style.display = 'none';
  if (kind === 'ok') {
    $('modal-h').textContent = '校验通过';
    $('modal-txt').innerHTML = '固件已经写进基站，读回来的内容和镜像一致。';
    setTxt2('现在，请拔掉基站，正常接电源。<br>可以按基站背面的按钮来选择频道（1、2、3、4）');
  } else if (kind === 'timeout') {
    $('modal-h').textContent = '没有完成校验';
    $('modal-txt').innerHTML = '超时没等到基站重新接上，这次没有校验。';
    setTxt2('下次刷写时会重新校验。');
  } else {
    $('modal-h').textContent = '校验没通过';
    $('modal-txt').innerHTML = '读回来的内容和镜像对不上。';
    setTxt2('建议重新刷一次。');
  }
  const btn = $('modal-ok');
  btn.disabled = false;
  btn.textContent = '确定';
  $('overlay').hidden = false;
  waitOpen = true;
}

$('btn-min').addEventListener('click', () => host?.Minimize());

// The button shows the language it will switch to, not the one in use.
function updateLangBtn() {
  const b = $('btn-lang');
  if (b) b.textContent = currentLang() === 'en' ? '中' : 'EN';
}
$('btn-lang').addEventListener('click', () => {
  setLang(currentLang() === 'en' ? 'zh' : 'en');
  updateLangBtn();
  // Everything visible is built from strings, so both the nav and the view have to be rebuilt
  // from scratch: the cached markup still holds the previous language.
  lastNavHtml = null;
  lastHtml = '';
  renderNav();
  renderView();
});
updateLangBtn();
// The label warning only closes on the button: it is the one thing the user must not skip.
$('modal-ok').addEventListener('click', () => {
  if (waitOpen && !waitDone) return;   // the re-plug gate has no manual close
  clearInterval(modalTimer);
  waitOpen = false;
  waitDone = false;
  $('overlay').hidden = true;
});
$('btn-close').addEventListener('click', () => host?.Close());
$('titlebar').addEventListener('mousedown', (e) => {
  if (e.button !== 0 || e.target.closest('.tb-btn')) return;
  host?.DragWindow();
});
window.addEventListener('resize', () => Map3D.resize());

(async () => {
  if (window.chrome?.webview) {
    window.chrome.webview.addEventListener('message', (ev) => {
      
      const m = ev.data || {};
      
      if (m.busy !== undefined) {
      
        flashBusy = !!m.busy;
        busyLabel = m.label || '';
        // The capture ended on the backend side (time limit, or the process died): the
        // button has to stop offering "stop" for something that is gone.
        if (!flashBusy) optRunning = false;
        paintBusyBar();
      
        renderView();
      
        return;
      
      }
      
      if (m.optical) {
      
        optical = m.optical;
      
        renderView();
      
        return;
      
      }
      
      if (m.level === 'wait') { showWaitNotice(0); }
      else if (m.level === 'wait-step') { showWaitNotice(1); }
      else if (m.level === 'wait-ok') { showWaitResult('ok'); }
      else if (m.level === 'wait-fail') { showWaitResult('fail'); }
      else if (m.level === 'wait-timeout') { showWaitResult('timeout'); }

      if (m.text) {
        if (m.op === '检查追踪质量') linkLog(m.text, m.level || '');
        else if (m.op && m.op.indexOf('驱动') >= 0) drvLog(m.text, m.level || '');
        else runLog(m.text, m.level || '');
      }
    });
  }
  if (host) {
    try { $('version').textContent = await host.Version(); } catch { }
    try {
      
      const forced = await host.StartPage();
      
      if (forced && NAV.some((n) => n.id === forced || (n.children || []).some((c) => c.id === forced))) route = forced;
    } catch { }
  }
  renderNav();
  log('openhouse setup · 演示版，只读');
  await refresh(false);
  const tick = async () => {
    await refresh(true);
    // The map page follows the tracker closely, the placement helper reads a one-second
    // counter, everywhere else two seconds is plenty.
    setTimeout(tick, route === 'base.map' ? 30 : route === 'track.quality' ? 150 : 2000);
  };
  setTimeout(tick, route === 'base.map' ? 30 : route === 'track.quality' ? 150 : 2000);
})();
