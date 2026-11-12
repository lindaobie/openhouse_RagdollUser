# openhouse · RagdollUser 修改版

> 基于 [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse)（MIT），
> 用于让 **4 台** HTC 一代基站同时工作（一代只有 4 个频道）。手上有 6 台，
> 可以轮换着用、也可以先刷好备用。所有内容集中在本目录。

---

## 目录结构

```
openhouse_FixedByRagdollUser\
├── README.md                     ← 本文件
├── docs\                         分析、计划、修复报告、基站台账
├── repo\openhouse\               源码（git 克隆，HEAD 817ea42，含我们的固件补丁）
├── shell\OpenhouseSetup\         图形界面（C# + WebView2，openhouse UI）
├── packages\
│   ├── firmware\                 固件刷写包（中文脚本 + 镜像）
│   └── driver\                   驱动安装包（v1.0.1 + 打补丁的安装器）
├── backups\                      额外留存的基站 dump
├── downloads\                    两个原始发布包
└── tools\                        采集脚本、编译脚本、临时分析脚本
```

---

## 关键事实

| 项目 | 值 |
|---|---|
| 上游仓库 | `github.com/YusufOzmen01/openhouse` |
| 源码 | 上游 `aaaaffc`（2026-11-11）+ 本仓库两个补丁 |
| 固件 | `lhbs-20261112-8D1687BBF161.bin`，51,208 字节（arm-none-eabi-gcc 13.2.1） |
| 驱动 | 同上源码，MSVC + vcpkg 静态编译，4.5 MB |
| 基站 | 6 台 1.0，两个生产批次（2016-05-05 / 2016-09-20）；同时最多跑 4 台 |

---

## 我们改了什么

### 1. 固件：激光电流兜底（`repo\...\src\laser.c`）

有些基站的 EEPROM 用户分区里 `laser.*.current = 1`（工厂分区是真实值），
lhbs 的功率环以 `boot_current ± 60` 为窗口，`1` 时够不到 30 mW，直接判故障、红灯常闪。

```c
if (P.laser_0_current < 40) P.laser_0_current = 190;
if (P.laser_1_current < 40) P.laser_1_current = 190;
```

健康机（117–172）完全不受影响。

### 2. 固件刷写脚本：汉化 + 时间戳标签 + 校验修复

`packages\firmware\...\Install-Firmware.ps1`（原版留在 `.orig`）

- 全中文，加了"会发生什么 / 需要准备什么 / 五步插拔"
- 不再问序列号，自动按时间戳备份，要求把编号贴到机身上
- **修了校验能跳过的竞态**：原来的 `Wait-Unplug` 只采一次样，`Eject` 后盘短暂消失就被放行，
  读回来的是缓存，校验形同虚设。现在要求连续 2 秒看不到盘
- 写入前把文件名 / 大小 / 哈希 / 目标盘列出来

### 3. 新增 `Restore-Stock.bat` / `Restore-Stock.ps1`

列出 `backups\` 里所有镜像，标注**原厂镜像**还是 **lhbs 固件**（靠构建串判断），选序号还原。
写入前读一遍现有固件，已经一致就跳过。

### 4. 驱动安装器：补上进程检查

`packages\driver\...\openhouse-driver.ps1`（原版在 `.orig`）

原来只查 `vrserver` / `monado-service`，但 **Steam 本体也锁着驱动的 DLL**（上游 issue #2）。
现在查 `steam / vrserver / vrmonitor / vrcompositor / monado-service`，并直接告诉你哪个进程挡着。

---

### 5. 图形界面（`shell\OpenhouseSetup`）

原本刷固件、装驱动、查摆位是三份散落的 PowerShell 脚本，靠翻文档记参数。
现在收进一个窗口：C# WinForms 外壳 + WebView2 渲染页面，单文件 exe，
目标机器不需要装 .NET 运行时。

**页面**

| 页面 | 做什么 |
|---|---|
| 主页 | 三张状态卡（SteamVR / openhouse / 接收器）+ 下一步该干什么 |
| 基站 / 基站状态 | 每台的频道、角色、锁定状态、定位误差 |
| 基站 / 刷写固件 | 四步引导接入 ISP 模式；只读检查、刷写、还原；写入后强制拔插校验 |
| 基站 / 位置查看 | Three.js 实时 3D：基站位置、朝向锥体、tracker |
| 基站 / 基站摆位助手 | 直接读接收器原始光脉冲，按频道报哪台没被照到 |
| 驱动 | 装 / 卸 openhouse 驱动，自动结束占用文件的进程 |
| 日志 | 内存记录 + 落盘文件，可复制、可打开目录 |
| 关于 | 原理说明 |

**几条刻意做对的地方**

- **窗口无边框，圆角交给 DWM**。CSS 的 `border-radius` 只圆页面内容，切不到
  窗口本身；`DWMWA_WINDOW_CORNER_PREFERENCE` 才能让整个窗口变圆。
- **选中行不改背景色**。Windows 的次像素抗锯齿会因为背景变化让字形发虚，
  所以列表用一条独立的滑块标记选中项，行本身不动。
- **按钮文字上移 2px**。CJK 字形的墨迹在行盒里偏下，直接居中是歪的。
- **日志区吸收布局变化**。状态栏出现时不让页面被压出滚动条，由日志区让出高度。
- **三套日志各自独立**。刷写、摆位、驱动各用一份缓冲，后端每条消息带操作名，
  不会出现“在摆位页看到刷写日志”。
- **摆位助手不依赖驱动**。直接读接收器的原始光脉冲，数每秒有多少次激光扫过，
  驱动坏了也能用；代价是它要独占接收器，所以会先把 SteamVR 关掉。
- **写入后不留退路**。刷完弹窗要求拔线重接，接上之前关不掉，避免“以为刷好了”。

### 6. 原始固件只备份一份

把 31 份历史 dump 逐字节比对之后确认：所有原厂固件的应用区完全相同。
存在两个版本（`90426752AD08` 与 `1C66483F3731`），但各自内部一致。
于是改掉了“每次写入前强制备份 128 KB”的做法：

- 首次使用时从任意一台原厂基站（或历史 dump）提取应用区，存成 `firmware\stock-<hash>.bin`
- 同一版本只存一份，不同版本各存一份
- 没有原始固件时，刷写与还原功能全部锁着
- 还原不再需要用户选备份，直接写回那一份
- `restore` 从“写整片 131072 字节”改成“只写应用区”，和刷写一致

### 7. 与上游的差异

当前整个源码树同步自上游 `aaaaffc`（2026-11-11），只保留这几处本地改动：

| 位置 | 改动 | 为什么 |
|---|---|---|
| `src/transport/usb/usb_transport.{hpp,cpp}` | 新增 `OnDeviceGone()` | 设备无线断开时没有清 `receivers_[dongle].connected`，SteamVR 的配对页因此一直说“控制器已连接”，换设备必须拔接收器 |
| `src/runtime/session.cpp` | `Base::lastSeen` + 8 秒超时 | `seen` 一旦置位永不复位，断电的基站会永远显示“正在定位” |

第二处与上游文档里的 `as requested` 设计相左（他们有意让基站常驻列表），所以只作本地行为，不提 PR。房间地图本身不受影响，仍旧缓存。

固件 `laser.c` 里还保留了一条上游没有的存量兜底：

```c
if (P.laser_0_current < 40) P.laser_0_current = 190;
```

根因（保存参数时把调节值写回 EEPROM）上游 11-08 已经修了，但已经被写坏到 `1` 的机器需要这两行才能自愈，否则刷新固件依旧是红灯。健康机（117–172）不受影响。

## 三条常用操作

### 用图形界面（推荐）

```
dist\publish\shell\openhouse-setup.exe
```

或者直接用 release 里那个：解压后双击 `openhouse-setup.exe`，`web` 文件夹要和它放在一起。

### 刷固件（命令行）

```
双击 packages\firmware\openhouse-basestation-firmware\Install-Firmware.bat
```

### 只校验（不写入）

```
双击 packages\firmware\openhouse-basestation-firmware\Verify-Firmware.bat
```

### 还原原厂

```
双击 packages\firmware\openhouse-basestation-firmware\Restore-Stock.bat
```

> **已经红灯的机器必须先还原原厂、上电跑几秒，再重新刷固件。**
> 直接重刷会出现"红灯没了但不追踪"的状态。这是实测结论，别简化。

### 装驱动

```
1. 完全退出 Steam（托盘图标也要退）
2. 双击 packages\driver\openhouse-windows-x64\Install-Driver.bat
3. 启动 SteamVR，打开同目录 apps\openhouse-viewer.exe
```

### 编译固件

```
tools\build-firmware.cmd          编译
tools\build-firmware.cmd clean    清理
```

依赖：`arm-none-eabi-gcc` 13.2.1、GNU Make 4.4.1、Python、git 自带的 `sh`。
路径写死在脚本里，换机器要改。

---

## 文档索引

| 文件 | 内容 |
|---|---|
| `docs\openhouse-分析报告.md` | 项目全貌：多站时序推导、固件、驱动、设置清单、对那个帖子的逐条核对 |
| `docs\openhouse-实施计划.md` | 分阶段计划：部署 → 测量 → 链路 → 权重调度，每阶段带 Gate |
| `docs\openhouse-修复报告-中文.md` | 激光电流 bug 的完整分析与修复（中文） |
| `docs\openhouse-issue-laser-current.md` | 同上，英文版，可直接贴上游 issue |
| `docs\基站台账.md` | 六台的标签 / 序列号 / 频道 / 备份对照 |

---

## 已知问题

| 问题 | 层面 | 状态 |
|---|---|---|
| 激光电流棘轮 → 红灯 | 固件 | ✅ 已修（本目录的固件） |
| 房间永不 `settled`（基站在没收敛时被锁死） | 驱动 | ✅ 已修（上游 `817ea42`，本目录的驱动已包含；v1.0.1 的官方 dll 没有） |
| tracker 关闭后换配另一台要重启 SteamVR | 驱动 | ✅ 已修（`OnDeviceDetached` 现在会释放接收器） |
| 断电的基站变成幽灵基站留在列表里 | 驱动 | ✅ 已修（`seen` 加 8 秒超时） |
| 红灯机器直接重刷 → 不追踪 | 固件 | ⚠ 机制仍未查明；GUI 里能一键走完「还原原厂 → 上电几秒 → 刷写」 |
| 基站被碰后两个宇宙来回跳（#4） | 驱动 | 未修 |
| 重捕获乒乓（#9，2.0 场景） | 驱动 | 未修，与一代基站无关 |
| 头显静止冻结（#7） | 驱动 | 未修 |
| 校验竞态、Steam 开着装失败 | 脚本 | ✅ 已修（等待连续 2 秒看不到盘；驱动页会自动结束占用进程的软件） |

---

## 提醒

- 刷写只覆盖前 49,408 字节，`0x1F000` 起的 EEPROM 副本和 `0x1A000` 起的快照都不碰。
  序列号和激光标定存在片内 EEPROM，跟 flash 是两块独立存储。
- 真正不可再生的是每台的 **EEPROM**（工厂标定）。lhbs 每次启动会把它的副本
  写到 flash 的 `0x1F000`，所以刷过 lhbs 的机器能从那块读出序列号。
- 原厂固件的两个版本都留在 `firmware\stock-*.bin`，还原时默认用文件名排前的那份。
