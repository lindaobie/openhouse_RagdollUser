# openhouse · RagdollUser 修改版

> 基于 [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse)（MIT），
> 用于让 6 台 HTC 一代基站同时工作。所有内容集中在本目录。

---

## 目录结构

```
openhouse_FixedByRagdollUser\
├── README.md                     ← 本文件
├── docs\                         分析、计划、修复报告、基站台账
├── repo\openhouse\               源码（git 克隆，HEAD 817ea42，含我们的固件补丁）
├── packages\
│   ├── firmware\                 固件刷写包（中文脚本 + 原厂备份 + 归档）
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
| 源码 HEAD | `817ea42` "Fixed bad basestation lock" |
| 固件发布基线 | `lhbs-20261108-db80251-dirty`（v1.0.1 官方包） |
| 驱动发布基线 | v1.0.1，`BUILD.json` 记 `22c3b22` + dirty，MinGW GCC 16.2 |
| 我们自己编的固件 | `lhbs-20261109-currentfix.bin`（arm-none-eabi-gcc 13.2.1） |
| 基站 | 6 台 1.0，两个生产批次（2016-05-05 / 2016-09-20） |

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

## 三条常用操作

### 刷固件

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
| 红灯机器直接重刷 → 不追踪，必须先还原原厂 | 固件 | ⚠ 机制未查明，流程已确定 |
| 房间永不 `settled`（基站在没收敛时被锁死） | 驱动 | ⚠ v1.0.1 **没有** `817ea42` 这个修复，master 有 |
| 基站被碰后两个宇宙来回跳（#4） | 驱动 | 未修 |
| 重捕获乒乓（#9，2.0 场景） | 驱动 | 未修，与一代基站无关 |
| 头显静止冻结（#7） | 驱动 | 未修 |
| 校验竞态、Steam 开着装失败 | 脚本 | ✅ 本地已修，可提上游 |

---

## 提醒

- `packages\firmware\...\backups\` 里的 14 个文件混着**原厂镜像**和 **lhbs 镜像**，
  用 `Restore-Stock.bat` 选的时候看"类型"那一列。
- 真正不可再生的是每台的 **EEPROM**（工厂标定），它在 `backups\` 里那几份带 `EEPROM` 字样的 dump 中，
  偏移 `0x1F040`。
- 刷机前、刷机后各留一份 dump，永远不亏。
