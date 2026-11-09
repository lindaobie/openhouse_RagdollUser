# openhouse 改造说明

**项目**：openhouse（HTC Vive 一代基站开源驱动与固件）
**上游**：`github.com/YusufOzmen01/openhouse`（MIT）
**本机目录**：`J:\workspace\openhouse_FixedByRagdollUser\`
**编制日期**：2026-11-09
**适用硬件**：HTC Vive 一代基站 6 台（两个生产批次），Vive Tracker，Watchman 接收器

---

## 摘要

本项目在上游 openhouse 的基础上完成三项工作：

1. **修复一处固件缺陷**：EEPROM 中激光驱动电流为坏值的基站，在 lhbs 固件下会误判故障、红灯常闪、永不发光。已通过源码补丁消除，六台基站全部恢复正常。
2. **补齐安装与诊断工具**：上游的刷写脚本与驱动安装器不满足实际使用需求（无状态检查、校验可被跳过、错误提示不明确），已重写并增加还原、校验、自检三类工具。
3. **形成可复现的构建与验证流程**：固件可由本机工具链从源码重新编译，镜像经向量校验并按物理写入验证。

上游源码的改动量为**一个文件、新增十一行**。其余内容均为独立于仓库的脚本、工具与文档。

---

## 1. 环境与版本

| 项目 | 值 |
|---|---|
| 上游提交 | `817ea42`（`Fixed bad basestation lock`） |
| 固件发布基线 | `lhbs-20261108-db80251-dirty`（官方 v1.0.1 包内镜像，46 512 字节） |
| 驱动发布基线 | v1.0.1，`BUILD.json` 记录 `22c3b22` + dirty，MinGW GCC 16.2 |
| 本机固件构建 | `arm-none-eabi-gcc 13.2.1`，产物 49 408 字节 |
| 固件构建依赖 | GNU Make 4.4.1、Python 3.14.6、Git 自带的 `sh` |
| 驱动构建环境（未启用） | MSVC 14.51（VS 2026 Build Tools）+ Windows SDK |

---

## 2. 对上游源码的修改

### 2.1 变更范围

```
 Modified   basestation_firmware/src/laser.c      +11 行，-0 行
```

仓库内其余文件均未改动。

### 2.2 变更内容

插入位置：`laser_init()`，在驱动电流写入 ISL58303 之前。

```c
/* Some units carry laser.0/1.current = 1 in their EEPROM user partitions: an older
 * firmware's channel-change saves wrote the power-regulated value back, so the
 * stored current ratcheted down while the factory partition still holds the real
 * one (one unit here: user 1/1, factory 127/112). Stock's power loop has no ceiling
 * and copes; ours caps at boot_current + 60, so such a base cannot reach 30 mW, is
 * declared faulted and turns into a red-blinking station that never emits.
 * A current below 40 cannot be a working setting (healthy units run 117-172 and the
 * stock default is 190), so treat it as missing and use the default. The value is
 * written back to the EEPROM by the next channel change, which heals the unit. */
if (P.laser_0_current < 40) P.laser_0_current = 190;
if (P.laser_1_current < 40) P.laser_1_current = 190;
```

### 2.3 缺陷机理

功率环的安全窗口锚定于 EEPROM 中保存的电流值：

```c
int hi = boot_current[n] + 60, lo = boot_current[n] - 60;
if (hi > 230) hi = 230;
if (target - pw > 1.0f) {
    if (*cur >= hi) { laser_fault = true; lasers_off(); return; }
    (*cur)++;
}
```

当 `laser.0.current` 被旧固件反复写入功率调节值而衰减至 1 时，窗口退化为 `[0, 61]`，
二极管无法在该电流下达到配置的 30 mW，功率环据此判定功率检测失效并锁死故障。
原厂固件的功率环不设上限，因而同一硬件在原厂固件下工作正常。

### 2.4 判定依据

对故障机 EEPROM（ISP 盘偏移 `0x1F040`）的完整比对显示，用户分区与工厂分区共 183 个
存储参数中仅有六项不一致：

| 参数 | 用户分区 | 工厂分区 | 性质 |
|---|---|---|---|
| `laser.0.current` | 1 | 127 | **异常** |
| `laser.1.current` | 1 | 112 | **异常** |
| `timebase.mode` | 4 | 3 | 多站模式状态，正常 |
| `mode.current` | 2 | 0 | 频道索引，正常 |
| `rotor.0.pwm` | 2017 | 2096 | 自动调谐值，正常 |
| `rotor.1.pwm` | 1767 | 2057 | 自动调谐值，正常 |

三个分区的 CRC32 均校验通过，排除数据损坏，确认为一次格式正确的坏值保存。

---

## 3. 新增文件

### 3.1 固件刷写包

路径：`packages\firmware\openhouse-basestation-firmware\`

| 文件 | 简介 |
|---|---|
| `Restore-Stock.ps1` | 原厂固件还原工具主逻辑。遍历 `backups\` 目录，依据镜像内的构建串区分「原厂镜像（`2016-xx-xx`）」与「lhbs 固件（`lhbs ...`）」，列出序号供选择，写入前做向量校验与容量校验 |
| `Restore-Stock.bat` | 上述脚本的启动器，供双击执行 |
| `Verify-Firmware.bat` | 只读校验入口。读取基站 flash 与镜像逐字节比对，并从 EEPROM 快照偏移 `0x1F050` 读取并显示 `LHB-` 序列号；不执行任何写入 |
| `Install-Firmware.zh.ps1` | 中文刷写脚本源文件，用于重新生成 `Install-Firmware.ps1` |
| `firmware\lhbs-20261109-currentfix.bin` | 本项目编译的固件镜像，包含第 2 节的补丁 |

### 3.2 驱动安装包

路径：`packages\driver\openhouse-windows-x64\`

| 文件 | 简介 |
|---|---|
| `openhouse-driver.zh.ps1` | 中文驱动安装器源文件，用于重新生成 `openhouse-driver.ps1` |

### 3.3 工具

路径：`tools\`

| 文件 | 简介 |
|---|---|
| `coverage.py` | 采集工具。连接驱动遥测端口 `127.0.0.1:36840`，解析换行分隔的 JSON 状态流，统计每个设备从每台基站获得的帧数与传感器行数，输出 CSV。用于评估基站布局的覆盖分布 |
| `Check-Driver.ps1` / `Check-Driver.bat` | 驱动自检。比对 SteamVR 当前加载的 `driver_lighthouse.dll` 与刷写包内镜像的哈希，判定是否为 openhouse 版本；SteamVR 更新或文件校验覆盖驱动时立即报告 |
| `Check-Flashes.ps1` / `Check-Flashes.bat` | 闪光布局采集。调用 `lhcapture --check` 采集 20 秒原始光脉冲，输出每个索引事件内的闪光次数、槽位偏移、各站的扫描占比与命中率，结果写入 `flashes.txt`。用于判定多站轮班是否成立 |
| `build-firmware.cmd` | 固件构建入口。设置工具链 PATH 并调用 `make -C basestation_firmware`，支持 `clean` 参数 |
| `bin\python3.exe` | 构建依赖垫片。Makefile 调用 `python3`，本机仅有 `python`，故提供同目录可执行文件 |

### 3.4 文档

路径：`docs\`

| 文件 | 简介 |
|---|---|
| `openhouse-分析报告.md` | 项目整体分析。包含多站时序的推导、固件与驱动的实现细节、完整设置清单，以及对流传帖文的逐条核对 |
| `openhouse-实施计划.md` | 分阶段实施计划。部署、测量、链路、权重调度四个阶段，每阶段附带验收条件 |
| `openhouse-修复报告-中文.md` | 激光电流缺陷的完整分析与修复说明（中文） |
| `openhouse-issue-laser-current.md` | 同上（英文版），按上游 issue 格式组织，可直接提交 |
| `基站台账.md` | 本机基站的身份对照表（机身序列号、`LHB-` 序列号、频道、批次、备份文件）。**含本机设备标识，不上传** |

### 3.5 数据存档

| 路径 | 内容 |
|---|---|
| `backups\` | 基站镜像存档。包含原厂镜像与带 EEPROM 快照的镜像，后者含工厂标定数据，不可再生 |
| `downloads\` | 官方发布包原件（固件包、驱动包），用于追溯与回滚 |

---

## 4. 改写的文件

以下文件在上游包内存在，本项目予以重写；原版均保留为同目录 `.orig` 文件。

### 4.1 `Install-Firmware.ps1`（固件刷写）

| 改动项 | 说明 |
|---|---|
| 界面语言 | 全中文 |
| 备份命名 | 取消序列号手动输入，改为按时间戳命名（`firmware-<yyyyMMdd-HHmmss>.bin`），并要求将该时间戳标注在基站机身 |
| 操作引导 | 增加「接下来会发生什么」「需要准备什么」「五步插拔流程」，并预先提示 Windows 的格式化弹窗 |
| 写入确认 | 写入前列出文件名、大小、SHA-256 前 16 位与目标路径 |
| 校验逻辑 | 修复竞态：原实现等待拔出时仅采样一次，`Eject` 后卷短暂消失即被判定为已拔出，随后读回的是缓存内容，校验失去意义。现要求连续 2 秒不可见方可放行 |
| 状态识别 | 若镜像已是 lhbs，提示本次备份保存的是 lhbs 而非原厂镜像 |

### 4.2 `openhouse-driver.ps1`（驱动安装）

| 改动项 | 说明 |
|---|---|
| 界面语言 | 全中文 |
| 安装前检查 | 读取目标 DLL 内容判定当前安装的是 openhouse 还是原厂驱动；已安装时报告版本一致性并询问是否重装（默认否） |
| 进程处理 | 检测 `steam`、`vrserver`、`vrmonitor`、`vrcompositor`、`monado-service` 并列出；征得同意后先调用 `steam.exe -shutdown` 正常退出，残留进程再强制结束，最后复检并报告自行重启的进程 |
| 完成提示 | 明确告知可重新启动 SteamVR，并给出自检工具路径 |

---

## 5. 系统环境变更

| 变更 | 位置 | 撤销方式 |
|---|---|---|
| 驱动替换 | `SteamVR\drivers\lighthouse\bin\win64\driver_lighthouse.dll`（8 029 184 字节） | 运行 `Uninstall-Driver.bat` |
| 原厂驱动备份 | 同目录 `driver_lighthouse.dll.stock`（2 756 248 字节） | — |
| 设置页模式文件 | `SteamVR\drivers\lighthouse\resources\settings\settingsschema.vrsettings` | 卸载时自动还原 |
| 更新检查器 | `%LOCALAPPDATA%\openhouse\openhouse-check.exe` | 卸载时删除 |

---

## 6. 验证记录

### 6.1 固件

| 项目 | 结果 |
|---|---|
| 编译 | `arm-none-eabi-gcc 13.2.1`，成功，无错误 |
| 向量校验 | `word7=0xefff0321`，校验和为零，通过 |
| 与官方镜像差异 | 源码一致；官方包构建于 2026-11-08 20:15:40，与上一提交仅差构建时间戳 |
| 写入验证 | 经 ISP 盘原地写入，断电重插后读回逐字节一致 |

### 6.2 基站

六台基站刷入后，其中四台因第 2 节的缺陷出现红灯。按「还原原厂 → 正常上电运行 → 重新刷入」流程处理后，四台全部恢复正常。

### 6.3 多站链路

三台基站接入后的闪光布局采集结果：

| 槽位 | 闪光可见率 | 扫描占比 | 命中率 |
|---|---|---|---|
| `@0 µs` | 100% | 33%（`1/3.0`） | 180 次/秒 |
| `@251 µs` | 100% | 33%（`1/3.0`） | 213 次/秒 |
| `@498 µs` | 100% | 33%（`1/3.0`） | 294 次/秒 |

三台按等分轮班，每组索引事件三次闪光，偏移稳定在 250 µs 网格上。

### 6.4 房间求解

```
room-1794234373   settled=True   learned=3   seen=3   epoch=1

基站 A  频道 2  learned  placed
基站 B  频道 3  learned  placed  locked
基站 C  频道 1  learned  placed  locked

tracker  basesSeen=3
    基站 A   frames=18   sensors=4.5
    基站 B   frames=20   sensors=7.4
    基站 C   frames=20   sensors=11.0
```

---

## 7. 未解决的问题

| # | 问题 | 现状 |
|---|---|---|
| 1 | 某台基站在主站模式下无法接收从站闪光，切换为从站后工作正常；更换主站后三台恢复等分轮班 | 现象与判据已确认，成因未查明。判据：闪光布局中主站的 `sweeps after` 为 100%（`1/1.0`）即表示它未统计到任何从站 |
| 2 | 驱动侧多站解码器在未激活时丢弃自身日志（`tracking_source.cpp` 的 `PopLog`），导致排查期间缺少线索 | 上游设计，未改动 |
| 3 | 驱动 v1.0.1 未包含提交 `817ea42`（基站在未收敛时即被锁定，房间无法进入 settled） | 未处理。若实际使用中出现该症状，需自行构建 master 分支的驱动 |
| 4 | 安装器校验存在竞态，可在未实际重插的情况下通过校验 | 本地已修复，未提交上游 |
| 5 | Steam 进程运行时安装驱动失败（上游 issue #2） | 本地已修复，未提交上游 |

---

## 8. 来源与许可

### 8.1 上游

本项目的全部固件与驱动源码均来自 **YusufOzmen01/openhouse**，以 MIT 许可发布：

```
MIT License

Copyright (c) 2026 YusufOzmen01
```

本仓库为其派生作品。MIT 许可允许使用、复制、修改、合并、发布、再许可与销售，条件是：
在任何副本或实质性部分中保留上述版权声明与许可全文。

上游 `LICENSE` 文件位于 `repo/openhouse/LICENSE`，分发时必须保留。

### 8.2 改动声明

相对上游源码的全部改动见本文件第 2 节：一个文件，新增十一行。
重写的安装脚本与新增工具见第 3、4 节。

### 8.3 不可分发的文件

以下内容不属于本项目的许可范围，**不得提交到公开仓库**：

| 内容 | 权利归属 | 说明 |
|---|---|---|
| `driver_lighthouse.dll.stock` | Valve Corporation | SteamVR 原厂驱动，位于 SteamVR 安装目录，不在本目录内 |
| `backups\` 下的基站镜像 | HTC | 基站原厂固件与工厂标定数据，含设备唯一信息 |
| `packages\firmware\**\backups\` | HTC | 同上 |

上游项目对此有明确约定：*Never ship Valve's files.*

随二进制分发的第三方库许可文件位于驱动包的 `licenses\` 目录（Ceres、Eigen、glog、hidapi、nlohmann-json、zlib、MinGW-w64 runtime 等），分发时需一并保留。

### 8.4 商标

本文件中的 Valve、SteamVR、HTC、Vive、Lighthouse 均为其各自权利人的商标。
本项目与 Valve Corporation 及 HTC Corporation 无关联、未获其背书或赞助。

---

## 附录 A 目录结构

```
openhouse_FixedByRagdollUser\
├── README.md
├── docs\                          分析、计划、修复报告、台账、本说明
├── repo\openhouse\                上游源码（HEAD 817ea42，含第 2 节补丁）
├── packages\
│   ├── firmware\                  固件刷写包
│   └── driver\                    驱动安装包
├── backups\                       基站镜像存档
├── downloads\                     官方发布包原件
└── tools\                         采集、自检、构建工具
```

## 附录 B 常用命令

| 操作 | 命令 |
|---|---|
| 刷写固件 | 双击 `packages\firmware\openhouse-basestation-firmware\Install-Firmware.bat` |
| 只读校验 | 双击 `packages\firmware\openhouse-basestation-firmware\Verify-Firmware.bat` |
| 还原原厂 | 双击 `packages\firmware\openhouse-basestation-firmware\Restore-Stock.bat` |
| 安装驱动 | 双击 `packages\driver\openhouse-windows-x64\Install-Driver.bat` |
| 驱动自检 | 双击 `tools\Check-Driver.bat` |
| 闪光布局采集 | 关闭 SteamVR 后双击 `tools\Check-Flashes.bat` |
| 覆盖统计 | `python tools\coverage.py 900 D:\coverage.csv` |
| 编译固件 | `tools\build-firmware.cmd` |

## 附录 C 变更履历

| 日期 | 内容 |
|---|---|
| 2026-11-09 | 建立文档；完成固件缺陷修复、六台基站部署、三台多站链路验证 |
