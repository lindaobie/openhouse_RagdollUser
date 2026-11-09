# openhouse / lhbs 深度分析报告

> 仓库：`github.com/YusufOzmen01/openhouse`（MIT）
> 本地克隆：`J:\workspace_AI_Agent\baibaizi\research\openhouse`
> 分析时间：2026-11-09
> 分析对象：commit `817ea42`（master tip，2026-11-08 20:34 +0300），release v1.0.1
> 仓库状态：32 commits，2026-10-22 建仓，114 stars / 10 forks / 10 issues，单人维护（YusufOzmen01）

---

## 0. 结论先行

1. **"四台 1.0 基站同时用"是真的**，但机制不是"四台同时扫描"，而是**整转轮转分时（revolution round-robin）**。同一时刻房间里只有一台基站在扫。
2. 它由两个独立的部分组成，**缺一不可**：
   - `basestation_firmware/`（lhbs）：给基站刷的自制固件，负责"四台排队"；
   - `src/`（openhouse 驱动）：替换 SteamVR 的 `driver_lighthouse`，负责"看懂这个排队"。
3. **代价是明确的**：总扫描率不变（60 次/秒/轴），四台平摊后每台 15 次/秒/轴。买到的是**视角数量和抗遮挡**，不是刷新率。这一点那份帖子完全没说。
4. 项目**非常年轻**：2.5 周、单人、作者自己承认重度 AI 辅助、v1.0.1 发布不到一天。功能是真的，成熟度不能当成熟产品看。
5. **1.0 基站刷不死**（NXP LPC11U37 的 ISP 在掩膜 ROM 里，靠按键进入，CRP 是 0xFFFFFFFF），但有"刷坏要手动救"的现实风险，而且已经有两个真实的翻车 issue。

---

## 1. 原版为什么只能认 2 台 —— 从物理层推

### 1.1 基站的时间结构

一代基站每转一圈 16.667 ms（`timebase.period = 800000` ticks @ 48 MHz），两个转子机械上差 180°，所以每转有**两个 index 事件**（转子 0 一次、转子 1 一次），间隔 8.333 ms。

每个 index 事件发一次**同步闪光（sync flash）**，宽度 62.5–135 µs，分 8 档：

```
宽度索引 = [skip << 2 | axis << 1 | data]
宽度 = 62.5 µs + 10.417 µs × 索引
```

接收端（手柄/头显上的光电二极管）只测**起始时刻和宽度**，不解调。宽度就是信道编码：`skip` 位告诉接收端"这一帧之后有没有人扫"，`axis` 告诉它是水平还是垂直，`data` 是 OOTX 数据流的 1 bit。

### 1.2 激光扫描窗口

```
laser.0: 32° → 156°   即  1.48 ms → 7.22 ms   窗口长 5.74 ms
laser.1: 25° → 149°   即  1.16 ms → 6.90 ms   窗口长 5.74 ms
```

（`params_table.h` 里 `laser_0_start=32.0, laser_0_length=124.0`）

### 1.3 为什么 4 台在原厂固件下不可能

- 激光窗口 5.74 ms **比四分之一转（4.17 ms）还长**。所以不能把一圈切成 4 份轮流扫 —— 两台会同时在房间里打光。
- 只能**整转分时**：一个 revolution 只有一台基站在扫。
- 两台为什么能共存：从站比主站错开 417 µs（`timebase.optical.offset = 20000` ticks）。两台的闪光组总共占 0.42 ms，而第一束激光 1.16 ms 才开始 —— 刚好塞得进去。
- 四台若沿用同样规则需要 1.25 ms，最后一台的闪光会落进别人的扫描窗；最后一台的扫描也要拖到下一个闪光组之前结束 —— 都不成立。

### 1.4 原厂本来就有 8 个频道，为什么不用

一代基站的按钮本来就循环 **A / b / c / 1 / 2 / 3 / 4** 八档。但 1–4 是 `master-async`：

```
1: 765053 ticks (62.7 Hz)    2: 836543 (57.4 Hz)
3: 874755 (54.9 Hz)          4: 914713 (52.5 Hz)
```

四台各自自由跑、靠频差打拍子。**接收端分不开重叠的脉冲** —— 两台的光同时打到同一个传感器上，脉冲就糊了。所以原厂驱动只用 A/b/c 双站模式，1–4 是给"多房间/独立空间"留的，不是给同一个房间的。

这个细节很重要：**硬件上早就有 4 个频道，缺的是"协调"这件事**。

---

## 2. lhbs 固件：协调是怎么做出来的

### 2.1 核心设计

```
N ∈ {2,3,4} 台，slot s ∈ {0..N-1}，slot 0 = 主站
第 r 转的扫描者 = 第 (r mod N) 个在场的 slot
主站在哪一转都闪；从站只在"该它扫"的那一转扫，但每转都闪
```

- **错开 250 µs**（`multi.stagger = 12000` ticks）：四台的闪光全塞进 0.75 ms，小于"第一束激光开始"的 1.16 ms。
- **skip 位语义完全不变**：`skip = 0` 仍然表示"这一帧之后我扫"。这一点是整个方案的巧妙处 —— **驱动不需要知道排班表，只要看 skip 位就知道谁在扫**。原厂的解码逻辑不用改。
- **OOTX 有效载荷不动**（v6，33 字节），靠 mode byte 的高位扩展：

```
byte 31 = 0x80 | (policy==ALL)<<6 | (N-1)<<4 | slot
byte 33 = 在场 slot 的 mask（多站模式才有，payload 变 34 字节）
```

### 2.2 N 不是配置的，是运行期自发现的

这一点 v1.0.0 到 v1.0.1 之间改过：

- 主站**自己也听光接收器**，每 100 ms 统计"过去 0.5 s 内哪些 slot 闪过"；
- 这个集合稳定 1 s 后，广播进 OOTX 第 33 字节；
- 每个从站在下一个超帧边界（rev % count == 0）切换。

结果是：**三台就三台轮（20 次/秒/轴），不会出现"四台排班有一台空转"**。

### 2.3 三层安全（这是全项目最值得看的部分）

多站最怕的是"两台同时扫"——激光撞在一起，接收端拿到一堆假数据，比丢帧严重得多。作者做了三层：

1. **不在 mask 里的站，或者还没解出 mask 的从站**：闪，但绝不扫。
2. **任何变更后从站静默 4 个超帧**：错过变更的从站会在 2 个超帧内看到"主站在不该扫的位置扫了"，于是停。
3. **每转一次证据检查**（`multi_sweeps_in()`）：从站要同时满足
   - 没看到比自己 slot 更早的站"说它在扫"，
   - 看到了主站"说它不扫"（或 100 ms 内见过主站），
   - 且 `rev % count == rank`。

第 3 层是最后加的 —— 作者用**四个 multi.c 实例同时跑 30 分钟随机上下电**（`test/host_multi.c`）时，有一个种子把前两层的漏洞跑出来了：一次"变更的暗号刚好也被丢掉"的情况。加了第 3 层之后，所有种子都干净。

这个测试方法值得一提：把固件模块编成 `.so`，用 `dlopen` 加载四份，每份一个虚拟基站，逐转驱动，随机丢 20–30% 的主站闪光。**纯软件仿真验证并发安全**，不需要四台真机。

### 2.4 同步质量

- 从站相位误差 **< 20 µs 连续 20 拍** 才允许发射，**> 40 µs 立刻停**。
- 实测从站闪光偏移在开机后 20 秒内从 235 → 269 → 250 µs 收敛。
- 主站丢了也要分情况：如果这转没看到主站"说 skip"，但 100 ms 内见过主站，仍然允许扫（因为相位还被锁着），只是会计数 `blind_sweeps`。

### 2.5 硬件这一层

固件不是打补丁，是**从 flash 镜像完整重写**：

| 项目 | 值 |
|---|---|
| MCU | NXP LPC11U37，Cortex-M0，12 MHz 晶振 → PLL → 48 MHz |
| 激光驱动 | Intersil ISL58303，三线 SPI 位敲（P1_22/P1_20/P1_1） |
| 载波振荡器 | LTC6904（I2C 0x16），1.8432 MHz |
| 加速度计 | LIS2DH（I2C），用来判断装反 + OOTX 重力向量 |
| 电机 | 2× 12-bit PWM，粗调 CT16B1 + 细调 CT16B0，带 PLL 锁相 |
| 时基 | CT32B1 自由跑，CAP0 抓外部/光同步 |
| 存储 | 4 KB EEPROM，IAP 命令 61/62，三个分区带 CRC32 |

作者把原厂固件的每个函数都对到了 flash 地址（`ai/firmware/HARDWARE.md` 里有完整 pin map 和函数表），参数表 185 项、551 字节 CRC 分区格式全部还原。**这是真正的逆向工程，不是猜的**。

### 2.6 版本坑：激光电流棘轮

这是我在代码里看到的最阴的一个坑，必须记下来。

`modes.c` 的 `mode_next()`（按钮切频道）每次都会 `params_save(0)` —— **原厂也这样**，切频道要存。问题在于：APC（自动功率控制）会把 `laser.0.current` 调到某个调节值，如果这个调节值被"顺手保存"，下次开机就以它为基准再调，几轮下来**电流棘轮到 1**，APC 的上限检查（`boot_current ± 60`）立刻失败 → 激光互锁 → **红灯常闪，永远不出光**。

代码里已经有针对性修复：

```c
void laser_boot_currents_for_save(bool saving)   // laser.c
{
  // 保存时写回开机电流，不写调节后的值
}
```

但**已经被写坏的 EEPROM 不会自己恢复**。issue #6 就是这个：用户的分区里 `laser.0.current = 1`，出厂分区是 190。他最后是二进制打补丁强制 `movs r2, #190` 救回来的。

**所以你刷之前，务必先备份四台的 flash（安装脚本自动做），并且知道救法**（见 §6.3）。

---

## 3. openhouse 驱动：看懂这个排队

### 3.1 它替换了什么

```
SteamVR\drivers\lighthouse\bin\win64\driver_lighthouse.dll        ← 被替换
SteamVR\drivers\lighthouse\bin\win64\driver_lighthouse.dll.stock  ← 原厂备份
```

关键设计：**openhouse 不含任何 Valve 的代码**。它只在自己需要头显显示（透镜畸变、显示模式）时，**在运行时加载那份 .stock**，而且只让它看头显。

```powershell
Install-Driver.bat     # 装
Uninstall-Driver.bat   # 还原
Install-Driver.bat -SteamVRPath "D:\SteamLibrary\steamapps\common\SteamVR"
```

安装器还会做三件事：
1. 往 `%LOCALAPPDATA%\openhouse\` 装一个 `openhouse-check.exe`，注册成 SteamVR 的自启动 app —— SteamVR 更新把驱动覆盖了，它会弹窗提醒；
2. 往 `SteamVR\resources\settings\settingsschema.vrsettings` 写 openhouse 设置页的 schema；
3. 算哈希校验装进去的 DLL。

### 3.2 三个解码器

```
src/tracking/disambiguator_tdm.cpp    原版 2 台（位级保留，oracle 对比）
src/tracking/disambiguator_multi.cpp  lhbs 2–4 台（全新）
src/tracking/disambiguator_sob.cpp    2.0 基站
```

`MultiDisambiguator` 的算法（`disambiguator_multi.cpp`）：

1. **合并闪光**：3 µs 内多个传感器看到的是同一次闪光；取宽度中位数。
2. **分组**：1 ms 内的是同一个 index 事件（一次闪光组）。
3. **定槽**：`slot = round((t - ref) / 250 µs)`，容差 **60 µs**；`ref` 是主站闪光时刻，每半转预测一次。
4. **重锚**：如果某台一直在参考点之前出现（且过去 64 组里出现过 30 次以上），把参考点前移，所有 slot 平移 —— 否则一次快速运动里的杂散脉冲会把整个编号推歪。
5. **OOTX 自校正**：每台解出自己的 OOTX，里面带着它自己的 slot；和相对编号不一致时，下一个闪光组整体平移。
6. **归属扫描**：`skip = 0` 的那台的样本落在 `[sync + P/12, sync + 5P/12]`（和原版一样），角度 `2π × (中点 - sync) / P`。

### 3.3 两个原版没有的细节

**（a）轴序列检查**

```cpp
// 一台站在第 h 半转扫 axis 0，那么第 h+1 半转必然扫 axis 1
const int expected = (lastSweepHalf == halfIndex_ - 1 && lastSweepAxis == 0) ? 1 : 0;
if (f.sync.axis != expected) continue;   // 丢掉
```

为什么需要：作者在 Beat Saber 录制里发现 A101B835 有 **9.5% 的 axis-0 扫描被解成 axis 1**（固件里 CT32B0 中断优先级低于 CT32B1，从站索引处理被推迟最多 168 µs，最短的 62.5 µs 闪光宽度被拉到 68 µs 以上，跨过了 axis 编码边界）。滤波器把水平角当垂直角用，**手柄会被甩出去 0.7–1.8 米**。固件侧已把 CT32B0 提到优先级 0，驱动侧留了这个检查当第二道保险。

**（b）idealPeriod 适配**

```cpp
const double interval = (上一次扫描间隔合理) ? gap : n * period;
f.idealPeriod = std::max(kIdealPeriod, interval);
```

原版的采样历史窗口按"同一个站每 16.7 ms 扫一次"设计的。多站模式下每个站 N 转才扫一次，所以帧里带的 `idealPeriod` 要换成"这个站的实际扫描间隔"，否则采样窗口全部错位。

### 3.4 跟踪滤波器和房间

**滤波器**：49 状态 EKF，`LighthouseDirect`，是 Valve `CLighthouseDirect` 的移植，用 oracle 测试和原厂二进制**逐位对比**。状态包括：

- 设备位姿、速度、角速度
- 主基站的倾角和重力方向
- IMU 标定（陀螺零偏、加计零偏/标度、IMU-本体安装误差）
- **最多 3 个其它基站相对主基站的位姿**（所以总共 4 台封顶）

**RoomMap**：所有设备共享一张房间图，而不是各自解各自的。

- 设备只要看到 **2 台以上**基站就存一个 keyframe；
- 大约每秒用最新的 keyframe 联合求解所有基站位姿；
- 某个基站攒够 100 个 keyframe 就**锁死**，之后光永远推不动它 —— 只有它自己的加速度计（真被碰了）能解锁；
- 每次重放一个基站，它的 `epoch` 变化，所有设备同步换关系；
- 平滑量：修正以 **2 cm/s、0.5°/s** 滑进来，设备看不到阶跃。

**这就是"Reset room"为什么能救命**：它把 `roommap.json` 丢掉重建。而 issue #4（基站被碰了一点，两个宇宙来回切）的根因正是"锁死的基站光推不动"——1.2° 以下的位移加速度计看不出来，于是新旧两个解同时存在，设备轮流跳。

### 3.5 相对原版的改进清单

| 改进 | 干什么 |
|---|---|
| 等迟到的光 | 无线 dongle 会把扫描推迟 20–25 ms，原版直接丢；openhouse 最多等 120 ms |
| 输出预测 + 平滑 | 修正不显形为跳变，默认 60 ms |
| 静止保持 | 不动的设备完全钉住，减少抖动 |
| 暴力运动冻结慢状态 | IMU 饱和时冻结基站倾角/IMU 标定这些慢变量，只让光修正运动 —— Beat Saber 类场景原版会直接丢跟踪 |
| 拒绝跳变帧 | 会把设备甩出 1 m 的帧直接不要 |
| Coasting | 遮挡时滑行而不是飘走 |
| 主站切换 | 多站房间里主站黑了 0.5 s 就换一台亮着的重新起 |

全部可以用 `LH_STOCK_FILTER=1` 关掉，回到原版行为。

---

## 4. 实际操作：完整清单

### 4.1 硬件前提

- 4 台 **1.0 基站**（9 灯或 15 灯都行，作者自己就混用）
- **micro-USB 数据线**（充电线不认，这是真坑）
- 每台都要能被另外的站"看见"（见 4.3）
- 一次刷一台，别同时插

### 4.2 刷固件（Windows）

```powershell
# 解压 openhouse-basestation-firmware.zip，双击
Install-Firmware.bat
```

脚本的流程：

1. 断电 → **按住背面按钮** → 插 micro-USB → 插电源 → 松手
2. 出现名为 `CRP DISABLD` 的盘（里面只有一个 `firmware.bin`）
3. 输入基站贴纸上的序列号（`LHB-XXXXXXXX`，用来给备份命名）
4. 脚本把当前固件**备份到 `backups\`**，然后**原地写入**（不是复制文件）
5. 再拔插一次（同样按住按钮）验证
6. 验证通过 → 断电 → 正常上电

**绝对不要用资源管理器把 firmware.bin 拖进去**。作者实测过：文件管理器复制会写错偏移，前 3 KB 丢失、整体后移 1 KB → 向量表变空 → 板子看着像死了。脚本用的是 `FileStream` + `WriteThrough`，512 字节对齐填充 0xFF。

### 4.3 设频道

按背面按钮循环：**A → b → c → 1 → 2 → 3 → 4 → 回到 A**，数码管显示当前频道。

```
一台设 1（主站 slot 0）
其余设 2 / 3 / 4（slot 1/2/3）
```

**关键约束，帖子说错了一半**：

> 不是"每个基站能看到主站"就够了，而是**双向**：主站必须能看到每个从站的闪光，从站也必须能看到主站的闪光。
>
> 主站靠"看到你的闪光"来把你算进轮班表（`multi_task()` 统计 0.5 s 内见过的 slot）。看不到，你就不在轮班里 → **你会闪，但永远不扫**。控制台会显示 "not in the round"。

所以摆位不能只考虑"从站视角看得到主站"，还要考虑"主站视角看得到从站"。房间里一台主站对着四个角落，主站的接收器可能看不到最远那台。

其他约束：

- **同步线在多站模式下无效**（`multi.h` 注释：sync cables do not work）
- **不能混用**：一个房间里要么全在 A/b/c，要么全在 1–4
- 用三台就 1/2/3，不用非凑满 4
- 切完频道建议**断电再上电一次**（author 遇到过 stale mode）

### 4.4 装驱动

```powershell
# 先把 Steam 完全退出（不是只退 SteamVR）
Install-Driver.bat
```

安装器只检查 `vrserver` 和 `monado-service` 两个进程 —— **但 issue #2 证明这不够**：Steam 本体持有 SteamVR 的 DLL 句柄，`Move-Item` 会失败：

```
Move-Item : Cannot create a file when that file already exists.
```

修复 PR（#5）还没合并，所以 v1.0.1 里这条**仍然存在**。手动结论：**退整个 Steam**，包括托盘图标。

### 4.5 SteamVR 里的设置

`设置 → openhouse`（可能要打开"高级设置"才看得到）：

| 设置 | 默认 | 说明 |
|---|---|---|
| Acceleration prediction | 开 | 快速运动时的外推，Beat Saber 类场景有用 |
| Position smoothing (ms) | 60 | 位置修正多久显形，0 = 关 |
| Rotation smoothing (ms) | 60 | 同上，旋转 |
| Record Session | 关 | 打开开始录，关掉时生成 bug report zip |
| Reset room | 关 | 丢掉学到的房间重建；是开关，点完自己关 |

改动**一秒内生效，不用重启**。

### 4.6 文件位置（Windows）

```
%APPDATA%\lh\roommap.json     学到的房间（删了 = Reset room）
%APPDATA%\lh\hostframe.json   游玩区域锚点（删了重新锚定）
%APPDATA%\lh\settings.json    可选：角色/控制器/禁用设备
%APPDATA%\lh\preinit\         可选：预初始化设备配置
%LOCALAPPDATA%\openhouse\recordings\   录制文件
文档\openhouse reports\        bug report zip
```

`settings.json` 长这样：

```json
{
  "roles": { "LHR-00000001": "generic_tracker" },
  "controllers": { "left": "LHR-00000002", "right": "LHR-00000003" },
  "disabledDevices": ["LHR-00000004"]
}
```

（上面的序列号是示位，实际写的是你自己设备的 `LHR-` 编号，在 `openhouse-viewer` 里能看到。）

角色可选 `left_hand` / `right_hand` / `generic_tracker`。**把 Vive 手柄当腰上 tracker 用**就是靠这个。

### 4.7 环境变量（一般都不用）

| 变量 | 作用 |
|---|---|
| `LH_LOG_LEVEL=DEBUG` | 打开调试日志 |
| `LH_MULTISTATION=on\|off` | 强制开关多站模式（自动检测失败时用） |
| `LH_STOCK_FILTER=1` | 关掉所有改进，回到原版跟踪 |
| `LH_CAPTURE=DIR` | 从启动开始整段录制 |
| `LH_NO_POWER_OFF_ON_EXIT=1` | 退出 SteamVR 不关设备 |
| `LH_STEAM_DB=auto` | 从 SteamVR 的 lighthousedb.json 导入基站位置 |
| `LH_HOST_FRAME=steamvr` | 用 SteamVR 自己的游玩空间，而不是 openhouse 的 |

### 4.8 第一次跑

1. 开 SteamVR，`openhouse-viewer`（`apps\` 里）打开看状态
2. 房间学习要 **30 秒左右**：拿着手柄在房间里走，让设备**同时看到 2 台以上**基站
3. 基站 LED 在"搜索"状态会闪，锁上之后变绿
4. 学完存进 `roommap.json`，下次秒进

---

## 5. 那份帖子逐条核对

| 帖子说的 | 判定 | 实际 |
|---|---|---|
| 原版只认 2 个 | ✅ | 对。1.0 的 TDM 解码器上限就是 2 |
| 刷固件 + 装驱动 | ✅ | 对，但要说清是两件独立的事 |
| micro-USB 必须是数据线 | ✅ | 对 |
| 装驱动前 Steam 要退干净，托盘也要退 | ✅ 但理由不对 | 安装器只查 vrserver；真正原因是 Steam 本体锁着 DLL（issue #2） |
| 验证不过再刷一遍 | ⚠️ 半对 | 根因是激光电流/APC 互锁，不是随机 bug。v1.0.1 已修；还红闪要按 issue #1 的办法（先刷回原厂跑一次再刷） |
| "H 盘要格式化"千万别点 | ✅ | 对，但更该说的是：**别用资源管理器拖文件** |
| 频道 A→b→c→1→2→3→4 | ✅ | 对 |
| 一个设 1，其他 2/3/4 | ✅ | 对 |
| 每个基站必须能直接看到主站 | ⚠️ **不完整** | **双向**：主站也必须看到每个从站，否则那台只闪不扫 |
| Reset room 治病 | ✅ | 对，issue #4 说明被碰过的基站确实需要 |
| 混搭玩家关 prediction、smoothing 拉 0 | ⚠️ 未证实 | 仓库没提 SpaceCalibrator。原理上讲得通（输出级会改姿态流），但建议**先别动**，有症状再关 |
| 4 基站全亮，哪都有 3-4 个看着你 | ⚠️ 误导 | 同一时刻**只有一台在扫**。总速率 60/s/轴不变，每台 15/s。买到的是视角，不是刷新率 |
| SteamVR 更新会覆盖驱动 | ✅ | 对，openhouse-check 会弹窗 |
| backups 别删 | ✅ | 对 |
| 零风险 | ❌ | 不是零风险。砖不了（掩膜 ROM ISP），但文件管理器拖拽会写坏 flash，EEPROM 电流棘轮会让你红闪 |
| "三个提醒" | — | 只列了两条，说明内容是 AI 生成后没校对 |

---

## 6. 已知问题与风险

### 6.1 仓库里挂着的 issue

| # | 标题 | 影响 |
|---|---|---|
| 1 | Need to flash twice | 首次刷入后激光不出、红灯闪；需"先刷回原厂跑一次再刷"。作者说 v1.0.1 已修 |
| 2 | Windows 安装因 Steam 打开失败 | **仍在 v1.0.1 里**，要退整个 Steam |
| 4 | 基站被碰后跟踪在两个宇宙间跳 | **未修**。得点 Reset room，然后重做房间 |
| 6 | 刷完后永久红闪，靠改 EEPROM 救回 | 电流棘轮，见 §2.6 |
| 7 | 头显静止时冻结、亚毫米修正是台阶 | 2.0 头显相关 |
| 9 | 重捕获乒乓（2.0、5 基站场景，2.75 次/秒） | 影响 2.0 用户，与你的 1.0 场景无关 |
| 10 | Monado 因缺 `capture` 字段失败 | 最新 commit 引入，release 里没有 |

### 6.2 项目成熟度

- **2.5 周，32 个 commit，一个作者**。作者在 README 里自己写了 AI disclaimer："heavily AI assisted... I've personally audited the codes it generated, especially on the firmware side"
- v1.0.1 发布至今不到一天，firmware asset 下载量 161
- 有好几个 issue 是**发布之后 7 小时内**报的，说明实际用户在踩坑
- 好处：作者响应很快（当天回、当天修）；有完整的 oracle 测试、replay 评测、主机仿真测试，工程纪律比"个人玩具"高一档

### 6.3 翻车了怎么办

**红闪（laser fault）**：

1. 先把基站切回 A/b/c，看是不是只在多站模式下闪
2. 进串口控制台（USB CDC，115200），看 `status` / `laser`
3. 查 EEPROM：进 ISP 模式读 `firmware.bin`，偏移 `0x1F040` 是 EEPROM 快照（`LHEEDUMP` 头 + 4032 字节）
4. 如果是 `laser.0.current = 1` 这类，`set laser.0.current 190` → `save`
5. 或者按 issue #1 的曲线办法：刷回原厂 → 上电跑一次 → 再刷 lhbs

**完全没反应**：

1. 断电，按住按钮，插 USB，插电，松手 → `CRP DISABLD` 应该出现
2. 脚本 `Install-Firmware.bat -Restore backups\LHB-XXXXXXXX-....bin`
3. 只要 CRP 是 0xFFFFFFFF（我们的镜像锁死了这一位）且能进 ISP，就砖不了

**刷完只在某些频道能出光**：先断电重启一次；还不行就换 USB 线（数据线不一定是好数据线）。

### 6.4 我对"值不值得刷"的判断

值得，但要按这个顺序：

1. **先刷一台**，设成 1（主站）+ 另一台原厂设 c，跑双站验证（这就是作者的 V1 验证项）
2. 跑通再刷第二台，设 2
3. 三台、四台
4. **每一步都留一份 `backups\` 的备份，并且复制到别的盘**

理由：物理上这个方案是成立的（时序预算算得过来），软件上有主机仿真 + oracle 测试兜着，但实际部署的坑（双向可见、EEPROM 电流、Steam 锁文件）都是"踩了才知道"的类型。

---

## 7. 对你自己的项目意味着什么

这部分不是帖子要的，是我看完代码后觉得更值钱的东西。

### 7.1 `liblh` 是一个独立的 C++ lighthouse 跟踪库

`include/lh/session.hpp` 这个 API 完全不依赖 SteamVR：

```cpp
lh::SessionOptions options;
options.roomMapPath = "room.json";
auto session = lh::Session::Create(options);
session->Start(&error);
for (const auto& e : session->PollEvents()) { ... }
auto snap = session->Snapshot();     // 每个设备的位姿、每个基站的位姿
```

MIT 许可，可以链接进自己的程序。`apps/example_app` 是最小示例（60 行）。

**对 RagTracker 的意义**：你现在做纯 IMU 动捕，缺的是"绝对参考"。四台 1.0 基站 + 几个 Tracker + `liblh`，可以搭一个**不依赖 SteamVR 的地面真值系统**，而且不需要买 2.0 基站。遮挡问题正好被四站解决。跑在 Linux 上（你的 NAS/ARM 服务器）也没有 SteamVR 依赖。

注意一个坑：`RoomPublisher` 的共享内存（`/openhouse-room-<uid>`）是 **`#ifdef __linux__`**，Windows 上没有。要做对齐工具得在 Linux 上跑，或者自己从 `Snapshot()` 拿基站位姿。

### 7.2 这个项目解决的是"协议层"，不是"射频层"

跟你之前在摸的基站无线协议、自制 dongle 是**上下两层**：

- 你做的是"dongle 怎么和基站/设备说话"（Watchman 无线协议）
- 这个项目做的是"基站之间怎么排队"（光同步 + OOTX）

两者可以叠加：自制 dongle + lhbs 四站 = 完全开源的 lighthouse 栈。而且 openhouse 的 `src/transport/usb/wireless_decoder.cpp` 就是 libsurvive 那套 Watchman 解码的 C++ 重写，可以直接读。

### 7.3 那个 250 µs 的取舍值得记下来

作者在 `MULTISTATION.md` §7 里列了被否决的方案，逻辑很干净：

- **90° 对切**：不可行，5.74 ms 的扫描塞不进 4.17 ms 的槽
- **频分**（原厂 1–4 频道）：不用协调，但会打拍子撞车，接收端分不开重叠脉冲
- **非活动站完全不闪**：光负载最低，但从站 N 转才见一次主站，同步会散
- **转更快 / 扫描窗更短**：容量上去了，电机应力和角度分辨率下来了

最后选了"整转轮转 + 每个 index 事件至少主站闪一次"。**用一点点光带宽换同步连续性** —— 这是典型的控制论取舍：不是优化单点指标，是保证闭环不散。

---

## 8. 代码地图（想深挖的时候看这里）

```
ai/README.md                      ← 给 AI 的项目交接文档，信息密度最高，先看这个
ai/firmware/HARDWARE.md           ← 基站硬件逆向全记录（pin map、函数地址、EEPROM 格式）
ai/firmware/MULTISTATION.md       ← 多站设计的完整推导 + 实测日志
ai/driver/SYSTEM_OVERVIEW.md      ← 驱动端到端
ai/driver/tracking_improvements.md← 每一条非原版改动的量化数据

basestation_firmware/src/multi.c       ← 轮班逻辑（470 行，最核心）
basestation_firmware/src/timebase.c    ← 时基/相位环
basestation_firmware/src/rotor.c       ← 转子 PLL（含中断优先级修复）
basestation_firmware/src/laser.c       ← ISL58303 + APC + 电流棘轮修复
basestation_firmware/src/modes.c       ← 频道表 / 按钮循环
basestation_firmware/test/host_multi.c ← 四站并发仿真

src/tracking/disambiguator_multi.cpp   ← 驱动侧多站解码
src/tracking/disambiguator_tdm.cpp     ← 原版 2 站解码（对照着看）
src/runtime/room_map.cpp               ← 房间图（锁、epoch、重新学习）
src/runtime/session.cpp                ← 会话层
src/openvr/driver_lighthouse.cpp       ← OpenVR 接口（67 KB）
src/openvr/stock_hmd.cpp               ← 运行时加载原厂 DLL 只给头显用
```

---

## 附：环境备注

你机器上 `git config --global http.proxy = socks5://127.0.0.1:10808`，但 **10808 端口没有监听**（代理客户端没起）。直连 github 是通的，我这次是用

```powershell
git -c http.proxy= -c https.proxy= clone ...
```

绕过去的。以后要么先开代理客户端，要么加这两个参数。
