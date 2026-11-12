// Bilingual UI. The Chinese text is the key: every string in the source stays Chinese, and
// English is a lookup. A missing entry falls back to Chinese instead of showing a raw id.
//
// Two paths, because most text is static but some carries numbers:
//   EN          whole text nodes ("主页", "已锁定")
//   EN_PATTERNS text with numbers or names ("3 台在线", "频道 2 的基站")
//
// translateDom() runs after every render in English mode. Nothing in the markup changes: only
// the visible text nodes are rewritten, so the code keeps reading in one language.

const EN = {
  // ---- navigation
  '主页': 'Home',
  '基站': 'Base stations',
  '基站状态': 'Status',
  '刷写固件': 'Flash firmware',
  '位置查看': 'Map',
  '追踪': 'Tracking',
  '追踪质量': 'Tracking quality',
  '驱动': 'Driver',
  '日志': 'Log',
  '关于': 'About',

  // ---- window
  '最小化': 'Minimize',
  '关闭': 'Close',

  // ---- shared states
  '已找到': 'Found',
  '未找到': 'Not found',
  '已锁定': 'Locked',
  '未在线': 'Offline',
  '就绪': 'Ready',
  '未锁定': 'Unlocked',
  '未定位': 'Unplaced',
  '已开启': 'Powered',
  '无故障': 'No faults',
  '收到光照': 'Light received',
  '锁定 tracker': 'Tracker locked',
  '原点': 'Origin',
  '主站': 'Master',
  '从站': 'Slave',
  '正常': 'OK',
  '未检测到': 'Not detected',
  '没有信号': 'No signal',
  '同步稳定': 'Stable',
  '间歇丢闪': 'Dropping',
  '偶尔出现': 'Rare',
  '采集中': 'Sampling',
  '未检查': 'Not started',
  '检查中': 'Checking',
  '停止检查': 'Stop',
  '开始检查': 'Start',
  '前往': 'Go',
  '前往驱动': 'Open driver',
  '前往基站': 'Open stations',
  '重新安装': 'Reinstall',
  '安装驱动': 'Install driver',
  '还原原厂驱动': 'Restore stock',
  '已停止': 'Stopped',
  '操作进行中': 'Working',
  '未知': 'Unknown',
  '原厂未替换': 'Stock driver',
  '文件缺失': 'Missing',
  '大小不符': 'Size mismatch',
  '未连接': 'Not connected',
  '操作记录会出现在这里。': 'Progress appears here.',
  '等上一步通过': 'Waiting for the step above',
  '详情': 'Details',

  // ---- table headers
  '频道': 'Ch',
  '序列号': 'Serial',
  '误差': 'Sigma',
  '解算帧数': 'Frames',
  '状态': 'State',
  '角色': 'Role',
  '设备': 'Device',
  '时间偏移': 'Offset',
  '出现比例': 'Seen',
  '轮转': 'Turn',
  '同步状态': 'Sync',
  '序号': 'Slot',

  // ---- tracking quality page
  '点击“开始检查”会自动关闭 SteamVR 相关进程': 'Starting a check closes the SteamVR processes',
  '检查会一直进行，数据实时刷新，直到你点停止或者满 10 分钟。':
    'The check keeps running and refreshes live until you stop it or ten minutes pass.',
  '可以在各个基站能照射到的范围内晃动 tracker 以查看追踪质量。':
    'Move the tracker through the area each station covers to see how tracking holds up.',
  '激光击中次数': 'Laser hits per second',
  '收不到基站的光': 'No light from base',
  '光到不了 tracker': 'Light misses tracker',

  // ---- home
  '没找到 SteamVR': 'SteamVR not found',
  '先确认 SteamVR 装在哪儿，其余的都依赖它': 'Locate the SteamVR install first; everything else depends on it',
  '当前用的是原厂驱动': 'The stock driver is in use',
  '原厂驱动不认多站，最多只能跑两台': 'The stock driver has no multi-station mode; two stations at most',
  '有一台基站正等着刷写': 'A station is waiting to be flashed',
  '它已经进了刷机模式，等你去刷固件': 'It is in flashing mode, waiting for firmware',
  '驱动在，但没有状态广播': 'Driver present, no state broadcast',
  'SteamVR 可能没启动，或者没跑到驱动这一步': 'SteamVR may not be running, or did not reach the driver',
  '基站都在，但没有设备在跟踪': 'The stations are up but nothing is tracked',
  '打开 tracker 或手柄，房间才能继续收敛': 'Turn on a tracker or controller so the room can settle',
  '房间还没收敛': 'The room has not settled',
  '有基站没定位好，看看是哪一台': 'A station is not placed; the status page shows which',
  '一切正常': 'All good',
  '没有定位到 SteamVR': 'SteamVR not located',
  '驱动页可以重新探测一次': 'The driver page can scan again',
  '把驱动换成 openhouse': 'Switch the driver to openhouse',
  '原厂驱动只支持两台基站': 'The stock driver supports two base stations at most',
  '启动 SteamVR': 'Start SteamVR',
  '驱动装好了，但没在跑，所以看不到基站': 'The driver is installed but not running, so no stations show up',
  '接上后按页面步骤操作': 'Connect it and follow the page',
  '打开 tracker 或手柄': 'Turn on a tracker or controller',
  '光有基站不够，得有设备出光，房间才学得到东西':
    'Stations alone are not enough: a device has to report light for the room to learn',
  '查明房间为什么不收敛': 'Find out why the room has not settled',
  '基站状态页会标出没定位的那台': 'The status page marks the station that is not placed',
  '看看基站摆在哪儿': 'See where the stations are',
  '3D 视角看方位和朝向': '3D view of position and heading',
  '暂时没什么要做的': 'Nothing to do right now',
  '系统在正常工作': 'Everything is working',
  '房间已收敛': 'Room settled',
  '房间未收敛': 'Room not settled',
  '接下来': 'Next',
  '原厂备份在位': 'Stock backup present',
  '没有原厂备份': 'No stock backup',
  '基站': 'Base stations',
  '驱动遥测没有响应': 'No telemetry from the driver',
  '没有状态广播': 'No state broadcast',
  '启动 SteamVR，基站状态才能读出来。': 'Start SteamVR so the base stations can be read.',

  // ---- station status page
  '没有收到这台基站': 'This station is not heard at all',
  '驱动连它的红外广播都没听到。先看电源和指示灯，再确认频道设过、背面按键没卡住。':
    'The driver does not even hear its broadcast. Check power and the indicator, then that the channel is set and the button on the back is not stuck.',
  '多半是激光功率上不去。已知的修法是：还原原厂固件 → 上电跑几秒 → 重新刷写。':
    'Usually the laser current never comes up. Known fix: restore the stock firmware, power it for a few seconds, then flash again.',
  '它在闪，但没有设备收到它的扫描': 'It flashes, but no device sees its sweep',
  '其它基站能被设备看到，唯独这台没有：它很可能收不到主站的光学同步。检查它和主站之间有没有遮挡、正面是否对着主站。':
    'Every other station is seen by a device; this one is not. It probably cannot see the master\'s optical sync: check for an obstruction and that its face points at the master.',
  '房间还没有它的位置': 'The room has no place for it yet',
  '有设备看到它，但视角不够。拿着 tracker 在那台基站前面晃动，多换几个位置。':
    'A device sees it, but from too few angles. Move the tracker around in front of that station.',
  '没有任何设备看到它。拿着 tracker 到那台基站正前方 1～2 米，对着它挥动——它很可能根本不在这个基站的视野里。':
    'No device sees it at all. Stand 1-2 m in front of that station and wave the tracker at it; it may simply not be in that station\'s field of view.',
  '正在定位，还没学完': 'Locating, not finished yet',
  '基站之间是通的，但 tracker 没被这台看到。拿着 tracker 走到那台基站正前方，对着它晃动，确认它在视野内。':
    'The stations see each other, but this one does not see the tracker. Walk in front of it and wave the tracker within its view.',
  '还在收敛，未锁定': 'Still settling, not locked',
  '能收到它的红外广播': 'Its infrared broadcast is heard',
  '没有硬件故障计数': 'No hardware fault count',
  '没有设备在线。先打开 tracker 或手柄。': 'No device online. Turn on a tracker or controller first.',
  '没有需要处理的基站': 'Nothing to fix on this station',
  'tracker 已被锁定': 'The tracker is locked',
  '位置 x / y / z': 'Position x / y / z',
  '定位误差': 'Placement sigma',
  '观测帧数': 'Solve frames',
  '故障计数': 'Fault count',
  '激光击中次数': 'Laser hits per second',

  // ---- flashing page
  '一次只接一台。写入只覆盖应用区，序列号和激光标定存在别的芯片里，不会被碰。':
    'One station at a time. Only the application area is written; the serial number and laser calibration live in another chip and are left alone.',
  '备份原始固件': 'Back up stock firmware',
  '刷写破解固件': 'Flash lhbs firmware',
  '还原原厂固件': 'Restore stock firmware',
  '与镜像对比': 'Compare with image',
  '状态': 'State',
  '原厂固件': 'Stock firmware',
  '破解固件': 'lhbs firmware',
  '停止': 'Stop',

  // ---- driver page
  '把 openhouse 驱动装进 SteamVR。安装会先结束 Steam 进程，并把 Valve 的原驱动保留成 .stock。':
    'Installs the openhouse driver into SteamVR. It stops the Steam processes first and keeps Valve\'s driver as .stock.',
  '当前': 'Current',
  '大小': 'Size',
  '原厂备份': 'Stock backup',
  '驱动文件': 'Driver files',
  '占用文件的进程': 'Processes holding it',
  '没有': 'None',
  '有': 'Yes',
  '正在执行，完成后状态会自动刷新。': 'Working; the state refreshes when it finishes.',
  'SteamVR': 'SteamVR',
  'openhouse': 'openhouse',

  // ---- log page
  '清空': 'Clear',
  '复制全部': 'Copy all',
  '打开日志目录': 'Open log folder',
  '日志目录不可写': 'Log folder is not writable',
  '每次启动会往软件目录下的 logs 文件夹写一个文本文件，保留最近 20 次。':
    'Each launch writes a text file into the logs folder next to the app; the last 20 are kept.',

  // ---- about
  '联系与源码': 'Contact and source',
  '贡献': 'Credits',
  '原作者': 'Original author',
  '源码修改和 UI 设计': 'Source changes and UI design',
  '仓库': 'Repository',
  '上游': 'Upstream',
  '实现原理': 'How it works',
  '给 HTC Vive 一代基站的图形化工具：刷固件、装驱动、看摆位。':
    'A graphical tool for HTC Vive 1.0 base stations: flash firmware, install the driver, check placement.',
  '基站为什么能超过两台。': 'Why more than two stations work.',
  '一代基站的扫描是时分复用：同一时刻只有一台在扫。改过的固件让 2～4 台各自占一个时间槽轮班，每台扫 1/N 的时间。原厂固件只支持两台，因为它只用了两个槽。':
    'A 1.0 station sweeps by time division: only one transmits at any moment. The modified firmware gives 2-4 stations a slot each, so every station sweeps for 1/N of the time. The stock firmware only knows two slots.',
  '驱动做了什么。': 'What the driver does.',
  'openhouse 驱动接管 SteamVR 的 lighthouse 驱动：读每个传感器的光电脉冲、解算出位姿，再按 SteamVR 的接口交出去。基站自己的位置和朝向也在这个过程中被解算出来，位置很准，朝向依赖角度的多样性。':
    'The openhouse driver takes over SteamVR\'s lighthouse driver: it reads each sensor\'s light pulses, solves the pose and hands it back through the SteamVR interface. The stations\' own positions and headings come out of the same solve; positions are solid, headings depend on how varied the angles were.',
  '刷写为什么只写 48 KB。': 'Why flashing only writes 48 KB.',
  '基站在 ISP 模式下会挂出一个 128 KB 的盘，那是整片 flash。我们的镜像只有 49,408 字节，是应用程序；写入时只覆盖开头，后面 81 KB 一个字节不碰——那里从 0x1F000 开始有 EEPROM 区，存着每台的序列号和激光标定。所以刷写不会把标定刷丢，也不能拿别人的备份整片写回去。':
    'In ISP mode a station mounts a 128 KB volume, which is the whole flash. Our image is the application only; writing covers the start of it and leaves the remaining 81 KB alone, where the EEPROM area from 0x1F000 holds each unit\'s serial number and laser calibration. Flashing therefore cannot lose the calibration, and someone else\'s dump cannot be written back wholesale.',
  '摆位助手为什么不用驱动。': 'Why the placement helper skips the driver.',
  '它直接读接收器的原始光脉冲，数的是"每秒有多少次激光扫到 tracker"。绕开驱动的好处是：驱动不工作时它照样能看，调试时这是唯一还看得见的眼睛。代价是接收器同一时间只能被一个程序占用，所以检查前得先把 SteamVR 退掉。':
    'It reads raw light pulses straight from the receiver and counts how many laser sweeps hit the tracker per second. Skipping the driver means it still works when the driver does not, which is the only eye left while debugging. The cost is that the receiver can only be held by one program, so SteamVR has to be closed first.',
  '这个工具本身。': 'The tool itself.',
  '刷写、驱动安装、摆位检查原本是几份 PowerShell 脚本，散在各个目录里。界面把它们收进来，但不重写其中已验证的逻辑：镜像向量校验、读保护检查、512 字节对齐原地写、先备份再写、强制断电重插后才校验，都是脚本里踩出来的规矩。':
    'Flashing, driver install and the placement check started as PowerShell scripts scattered across folders. The UI gathers them but does not rewrite the logic they already proved: vector checks, the read-protection check, 512-byte aligned in-place writes, back up before writing, and verify only after a forced power cycle.',
  '和他可爱的 DeepSeek 白白子': 'and his lovely DeepSeek Baibaizi',
  'Bilibili：RagdollUser': 'Bilibili: RagdollUser',
  'openhouse setup': 'openhouse setup',
  '主基站': 'Master',
  '未开始检测': 'Not started',
  '会自动关闭 SteamVR 相关进程': 'closes the SteamVR processes',
  'SteamVR 正在运行': 'SteamVR is running',
  '原厂备份在位': 'Stock backup present',
  '台基站': 'stations',
  '个设备': 'devices',
  '学习': 'learned',
  '看到': 'seeing',
  '台在线': 'online',
  '个时间槽': 'slots',
  '正在执行': 'Working',
};

// Text that carries numbers or names. First match wins.
const EN_PATTERNS = [
  [/^(\d+) 台在线$/, '$1 online'],
  [/^(\d+) 台$/, '$1 stations'],
  [/^(\d+) 个设备$/, '$1 devices'],
  [/^(\d+) 个时间槽$/, '$1 slots'],
  [/^(\d+) 条$/, '$1 lines'],
  [/^频道 (\S+)$/, 'Channel $1'],
  [/^频道 (\S+) 的基站$/, 'Station on channel $1'],
  [/^房间已收敛，(\d+) 台基站、(\d+) 个设备在工作$/, 'Room settled, $1 stations and $2 devices working'],
  [/^有 (\d+) 个设备看到它，但帧数不够。对着那台基站来回晃动 tracker，让它持续处在视野里。$/,
    '$1 devices see it, but not enough frames. Wave the tracker in front of that station and keep it in view.'],
  [/^已经有 (\d+) 个设备看到它，但帧数不够。对着那台基站来回晃动 tracker，让它持续处在视野里。$/,
    '$1 devices see it, but not enough frames. Wave the tracker in front of that station and keep it in view.'],
  [/^误差 (\S+) mm，要到 20 mm 以内。到那台基站正前方对着它晃动 tracker，静止和背对着都不算。$/,
    'Sigma $1 mm, needs to be under 20 mm. Wave the tracker in front of that station; standing still or facing away does not count.'],
  [/^(\d+) 个设备能收到它的光$/, '$1 devices receive its light'],
  [/^刷写 (\d+) 台待刷写的基站$/, 'Flash $1 stations'],
  [/^· 学习 (\d+) 台 · 看到 (\d+) 台$/, '· learned $1 · seeing $2'],
  [/^学习 (\d+) 台 · 看到 (\d+) 台$/, 'learned $1 · seeing $2'],
  [/^没有收到频道 (\S+) 的基站。检查电源、背面指示灯和频道设置。$/,
    'No signal from channel $1. Check power, the indicator on the back and the channel setting.'],
  [/^频道 (\S+) 报了 (\d+) 个故障，先还原原厂固件再刷回来。$/,
    'Channel $1 reports $2 faults; restore the stock firmware and flash it again.'],
  [/^频道 (\S+) 在闪，但没有设备收到它的光。检查它和主站之间有没有遮挡、正面是否对着主站。$/,
    'Channel $1 flashes but no device receives its light. Check for an obstruction between it and the master, and that its face points at the master.'],
  [/^没有设备收到频道 (\S+) 的光。把 tracker 拿到它的照射范围内，持续晃动数秒以定位。$/,
    'No device receives light from channel $1. Move the tracker into its beam and wave it for a few seconds.'],
  [/^频道 (\S+) 没有锁定 tracker，请拿着 tracker 在它的照射范围内晃动数秒，直至基站锁定 tracker。$/,
    'Channel $1 has not locked the tracker. Wave the tracker inside its beam until it does.'],
  [/^这台报了 (\d+) 个故障$/, 'This station reports $1 faults'],
  [/^查看详情$/, 'Details'],
  [/(\d+) 台(?=\s|·|$)/g, '$1'],
  [/^(\d+) 台基站都在同一套时序里，间隔连续，没有缺席。$/, 'All $1 stations share one timebase with even spacing.'],
];

let lang = 'zh';
try { lang = localStorage.getItem('oh_lang') || 'zh'; } catch { lang = 'zh'; }

function currentLang() { return lang; }

function translateText(text) {
  if (lang !== 'en' || !text) return text;
  const key = text.trim();
  if (!key) return text;

  const exact = EN[key];
  if (exact !== undefined) return text.replace(key, exact);

  // Whole-sentence patterns only. The global ones are for fragments and run at the end, so
  // a bare counter inside a longer sentence does not short-circuit the phrase pass.
  for (const [re, rep] of EN_PATTERNS) {
    if (re.global) continue;
    if (re.test(key)) return text.replace(key, key.replace(re, rep));
  }

  // No whole sentence match: several strings are assembled from pieces ("1 · 已开启",
  // "房间已收敛 · 学习 3 台 · 2 个设备"). Replace the known phrases inside them, longest
  // first so "基站状态" wins over "基站". Only for short labels: on a paragraph this would
  // splice English into the middle of a Chinese sentence, which reads worse than leaving it.
  if (key.length > 24) return text;
  let out = text;
  let hit = false;
  const keys = Object.keys(EN).filter((k) => k.length >= 2).sort((a, b) => b.length - a.length);
  for (const zh of keys) {
    if (out.indexOf(zh) < 0) continue;
    out = out.split(zh).join(EN[zh]);
    hit = true;
  }
  if (!hit) return text;
  // The pieces left a bare counter ("learned 3 台"): settle those too.
  for (const [re, rep] of EN_PATTERNS) {
    if (re.global) out = out.replace(re, rep);
    else if (re.test(out)) out = out.replace(re, rep);
  }
  return out;
}

function t(zh) {
  if (lang !== 'en') return zh;
  return EN[zh] ?? zh;
}

// Rewrites the visible text under `root`. Called after a render in English mode, so the markup
// itself never carries translations and the switch is just "render again".
function translateDom(root) {
  if (lang !== 'en' || !root) return;
  const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
  const nodes = [];
  while (walker.nextNode()) nodes.push(walker.currentNode);
  for (const node of nodes) {
    const next = translateText(node.nodeValue);
    if (next !== node.nodeValue) node.nodeValue = next;
  }
  // Placeholders and titles live in attributes, which the walker does not reach.
  for (const el of root.querySelectorAll('[title]')) {
    const next = translateText(el.getAttribute('title'));
    if (next !== el.getAttribute('title')) el.setAttribute('title', next);
  }
}

function setLang(next) {
  lang = (next === 'en') ? 'en' : 'zh';
  try { localStorage.setItem('oh_lang', lang); } catch { }
  try { document.documentElement.lang = lang === 'en' ? 'en' : 'zh-CN'; } catch { }
}

export { t, currentLang, setLang, translateDom, translateText };
