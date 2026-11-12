# openhouse · RagdollUser 修改版

**中文** | [English](README.en.md)

> 基于 [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse)（MIT），
> 让 **4 台** HTC Vive 一代基站同时工作（一代只有 4 个频道），并配一套图形界面。

---

## 这是什么

```
下载 → 解压 → 双击 openhouse-setup.exe
```

不需要装 .NET 运行时，单文件 exe。界面里可以刷固件、装驱动、看基站状态、在 3D 里看基站摆位、检查多站同步质量。

**中英文可切换**：标题栏右上角的按钮在 `EN` 和 `中` 之间切，选择会记住。

---

## 快速上手

### 1. 刷固件

```
图形界面 → 基站 → 刷写固件
```

四步接入 ISP 模式（断电 → 按住背面按钮 → 插 micro-USB → 插电源再松手），
页面会自己检测 CRP 盘。写入前会要求先把原厂固件备份一份——没有备份，刷写和还原都锁着。

> **已经红灯的机器必须先还原原厂、上电跑几秒，再重新刷固件。**
> 直接重刷会出现"红灯没了但不追踪"的状态。这是实测结论。

### 2. 装驱动

```
图形界面 → 驱动 → 安装驱动
```

会自动结束占用驱动文件的进程（Steam 本体也会锁 DLL）。Valve 的原驱动会保留成 `.stock`，随时可以还原。

### 3. 检查摆位

```
图形界面 → 基站 → 位置查看    3D 看基站方位和朝向锥体
图形界面 → 追踪 → 追踪质量    看各基站有没有排进同一套时序
```

追踪质量页看的不是 tracker 在哪，而是各基站有没有排进同一套时序：时间偏移、出现比例、轮转比例、同步状态。缺哪台、哪台在丢闪，一眼能看出来。

---

## 目录结构

```
openhouse_with_GUI\
├── openhouse-setup.exe          图形界面（自包含单文件）
├── web\                         界面资源
├── packages\
│   ├── driver\                  驱动 + 工具 + 安装脚本
│   └── firmware\                固件镜像 + 刷写脚本
├── docs\                        分析、计划、修复报告
├── tools\                       采集与编译脚本
├── README.md                    中文说明（本文件）
├── README.en.md                 英文说明
└── 启动说明.txt
```

---

## 关键事实

| 项目 | 值 |
|---|---|
| 上游仓库 | `github.com/YusufOzmen01/openhouse` |
| 源码基线 | 上游 `aaaaffc`（2026-11-11）+ 本仓库补丁 |
| 固件 | `lhbs-20261112-D930256358A9.bin`，51,192 字节（arm-none-eabi-gcc 13.2.1） |
| 驱动 | 同源码，MSVC + vcpkg 静态编译，4,528,128 字节 |
| 基站 | 一代 1.0，同时最多跑 4 台（固件只支持 4 个频道） |

---

## 我们改了什么

### 固件

**没有本地补丁。** 上游 11-10 在 `params.c` 里加了 `repair_laser_currents()`：开机时如果发现激光电流不在 80–230 区间，就从**工厂分区**恢复真实标定，再写回用户分区。这比早先那个 `< 40 → 190` 的兜底更准，所以本地补丁已经移除。

### 驱动（相对上游 `aaaaffc`）

| 位置 | 改动 | 为什么 |
|---|---|---|
| `src/transport/usb/usb_transport.{hpp,cpp}` | 新增 `OnDeviceGone()` | 设备无线断开时没有清 `receivers_[dongle].connected`，SteamVR 配对页因此一直说"控制器已连接"，换设备必须拔接收器 |
| `src/runtime/session.cpp` | `Base::lastSeen` + 8 秒超时 | `seen` 一旦置位永不复位，断电的基站会永远显示"正在定位" |
| `src/openvr/driver_lighthouse.cpp` | `PublishState` 20 Hz → 60 Hz | 位置查看页按屏幕刷新率绘制，20 Hz 的数据意味着三帧里有两帧用旧位姿，看起来就是拖影 |

第二处与上游文档里的 `as requested` 设计相左（他们有意让基站常驻列表），所以只作本地行为。

### 工具

| 位置 | 改动 | 为什么 |
|---|---|---|
| `apps/lhcapture/main.cpp` + `sync_monitor.hpp` | 报表 1 秒 → 50 毫秒，统计窗口 4 秒 → 0.8 秒 | 原来那串缓慢变化的平均值看起来像卡住 |

### 图形界面（`shell/OpenhouseSetup`）

原本刷固件、装驱动、查摆位是三份散落的 PowerShell 脚本，靠翻文档记参数。现在收进一个窗口：C# WinForms 外壳 + WebView2 渲染页面。

| 页面 | 做什么 |
|---|---|
| 主页 | 三张状态卡（SteamVR / 驱动 / 基站）+ 下一步该干什么 |
| 基站 / 基站状态 | 每台的频道、角色、锁定状态、定位误差、解算帧数 |
| 基站 / 刷写固件 | 四步引导接入 ISP 模式；只读检查、刷写、还原；写入后强制拔插校验 |
| 基站 / 位置查看 | Three.js 实时 3D：基站位置、朝向锥体、tracker |
| 追踪 / 追踪质量 | 直接读接收器原始光脉冲，看各基站的时序与同步质量 |
| 驱动 | 装 / 卸 openhouse 驱动，自动结束占用文件的进程 |
| 日志 | 内存记录 + 落盘文件，可复制、可打开目录 |
| 关于 | 原理说明 |

**几条刻意做对的地方**

- **窗口无边框，圆角交给 DWM**。CSS 的 `border-radius` 只圆页面内容，切不到窗口本身；`DWMWA_WINDOW_CORNER_PREFERENCE` 才能让整个窗口变圆。
- **选中行不改背景色**。Windows 的次像素抗锯齿会因为背景变化让字形发虚，所以列表用一条独立的滑块标记选中项。
- **按钮文字上移 2px**。CJK 字形的墨迹在行盒里偏下，直接居中是歪的。
- **数字原地更新**。计数器、表格数字都只改文本节点，不重建 DOM——否则滚动位置会丢，按钮会在按下和抬起之间被换掉，点击就丢了。
- **三套日志各自独立**。刷写、摆位、驱动各用一份缓冲，不会出现"在摆位页看到刷写日志"。
- **写入后不留退路**。刷完弹窗要求拔线重接，接上之前关不掉，避免"以为刷好了"。

---

## 三条常用操作（命令行）

```
packages\firmware\openhouse-basestation-firmware\Install-Firmware.bat   刷固件
packages\firmware\openhouse-basestation-firmware\Verify-Firmware.bat    只校验
packages\firmware\openhouse-basestation-firmware\Restore-Stock.bat      还原原厂
packages\driver\openhouse-windows-x64\Install-Driver.bat                装驱动
```

编译固件：`tools\build-firmware.cmd`。依赖 `arm-none-eabi-gcc` 13.2.1、GNU Make 4.4.1、Python、git 自带的 `sh`。

---

## 已知问题

| 问题 | 层面 | 状态 |
|---|---|---|
| 激光电流棘轮 → 红灯 | 固件 | 已修（上游 `repair_laser_currents()`） |
| 房间永不 `settled`（基站在没收敛时被锁死） | 驱动 | 已修 |
| tracker 关闭后换配另一台要重启 SteamVR | 驱动 | 已修 |
| 断电的基站变成幽灵基站留在列表里 | 驱动 | 已修（`seen` 8 秒超时） |
| 红灯机器直接重刷 → 不追踪 | 固件 | 机制仍未查明；GUI 里能一键走完「还原原厂 → 上电几秒 → 刷写」 |
| 某台基站当主站时收不到从站闪光 | 固件 | 未查，可换一台当主站绕过 |

---

## 提醒

- 刷写只覆盖应用区（当前 51,192 字节），`0x1F000` 起的 EEPROM 副本不碰。序列号和激光标定存在芯片内 EEPROM，跟 flash 是两块独立存储。
- 真正不可再生的是每台的 **EEPROM**（工厂标定）。lhbs 每次启动会把副本写到 flash 的 `0x1F000`，所以刷过 lhbs 的机器能从那块读出序列号。
- 原厂驱动会被保留成 `.stock`，驱动页可以一键还原。
