# openhouse 闭环调度 · 实施计划

> 目标：让 4 台 1.0 基站按"实际可见性"分配扫描时间，而不是现在的等权轮转。
> 前提：硬件零改动，全部在软件层。
> 制定：2026-11-09

---

## 总览

```
阶段 0  基线测量        0 改动      半天      → Gate A：值不值得做
阶段 1  工具链          0 改动      半天      → Gate B：能不能编译
阶段 2  固件开关        改固件      1 天      → Gate C：链路通不通
阶段 3  驱动链路        改驱动      2–3 天    → Gate D：能不能自动控制
阶段 4  权重调度        改两者      1 周      → Gate E：有没有真实提升
阶段 5  验证与收敛      改两者      持续
```

每个 Gate 都是**可以停下来的地方**。不通过就停，不硬推。

---

## 前置 · 部署（刷 lhbs）

> 四个基站还在原厂固件上时，SteamVR 只认 2 台，多站房间不存在，后面的可见性矩阵也无从谈起。
> **这一节没做完，阶段 0 之后的任何测量都是无效数据。**

### P.1 准备

- [x] 4 台 1.0 基站（9 灯 / 15 灯都行）
- [x] micro-USB **数据线**（充电线插上去盘不会出现，这是最常见的卡点）
- [x] 一次只接一台，别同时插
- [x] 记下每台贴纸上的序列号（`LHB-XXXXXXXX`），刷机时要输入，用来给备份命名

官方包已经下好了：

```
J:\workspace_AI_Agent\baibaizi\research\tmp\fw\openhouse-basestation-firmware\
```

### P.2 刷第一台（先只刷一台）

双击 `Install-Firmware.bat`，然后：

1. 拔掉这台基站的电源
2. **按住背面那个按钮不放**
3. 插上 micro-USB 到电脑
4. 插上电源，然后松手
5. 资源管理器里出现一块叫 `CRP DISABLD` 的盘（里面只有一个 `firmware.bin`）
6. 脚本问序列号 → 输入贴纸上的那个
7. 脚本把原厂固件备份到 `backups\` 然后原地写入 → 输 `y` 确认
8. 拔下来，**再按同样方式接一次**，脚本会做校验
9. 校验通过 → 断电 → 正常插电（不按按钮）

**三条禁令：**

- Windows 弹「需要格式化」→ **千万别点**，直接关掉那个窗口
- **绝对不要**用资源管理器把 firmware.bin 拖进那个盘（会写错偏移，前 3 KB 丢失，板子看起来像死了）
- **不要删 `backups\` 文件夹**，那是每台唯一的原厂镜像

### P.3 设频道

按背面按钮循环：`A → b → c → 1 → 2 → 3 → 4 → A`，数码管显示当前值。

- [x] 第一台设成 `1`（主站）
- [x] 断电再上电一次（作者遇到过 stale mode，切完重启最稳）
- [x] 上电后红灯常闪？见下面的排查
- [x] 正常应该是绿灯，数码管显示 `1`

### P.4 刷第二台，验证双站

- [ ] 刷第二台，设成 `2`
- [ ] 两台都上电，观察：两台都在闪同步光，其中一台在扫（肉眼看不出来，用手机摄像头能看到红外）
- [ ] 这时才能装驱动

### P.5 装 openhouse 驱动

```powershell
# 先把 Steam 完全退出（不是只退 SteamVR，托盘图标也要退）
Install-Driver.bat
```

安装器只检查 `vrserver` 和 `monado-service` 两个进程，但 Steam 本体锁着 DLL——issue #2 就是这么失败的，修复 PR 还没合。

- [ ] 装完启动 SteamVR
- [ ] `openhouse-viewer` 里两台基站都出现，channel 显示 `1` 和 `2`
- [ ] 手柄能出位姿

### P.6 刷剩下两台

- [ ] 第三台设 `3`，第四台设 `4`
- [ ] 四台都在 `openhouse-viewer` 里 learned
- [ ] 房间学习 30 秒左右（拿着手柄走动，让设备同时看到 2 台以上）

### ⚠️ v1.0.1 驱动的一个已知问题

发布三分钟后作者推了一个修复（`817ea42`），**没进 v1.0.1 的包**：

> 基站可能在还没收敛时就被「锁定」，之后求解器再也不修正它。实测一台锁在 50 mm 误差上，
> 导致房间永远不进入 settled 状态，对齐工具一直等。

**症状**：`openhouse-viewer` 里基站显示 learned，但 `room.settled` 一直是 false。

**如果出现**：先别急着刷固件或在驱动上动刀，那是驱动层的 bug，构建 HEAD 或等 v1.0.2。
跟踪本身不受影响，受影响的是对齐工具（SpaceCalibrator 这类）。

---

## 阶段 0 · 基线测量（0 改动，半天）

### 目的

搞清楚"你的房间到底需不需要这套东西"。**这一步省了，后面所有工作都可能是在优化一个不存在的问题。**

### 0.1 确认现状（30 分钟）

- [ ] 四台基站都刷了 lhbs，频道 1/2/3/4，都在 `1`–`4` 多站模式
- [ ] `openhouse-viewer` 里四台状态都是 `learned`
- [ ] 房间已学习完成（`roommap.json` 已存在）
- [ ] 至少有一次稳定使用超过 10 分钟，没有异常掉跟踪

**如果这四条有任何一条不满足，先回到基础部署，不要往下走。**

### 0.2 采集脚本（1 小时）

驱动的遥测口是明文 TCP + 换行 JSON，`127.0.0.1:36840`，20 Hz。协议见
`include/lh/telemetry.hpp`，其中 `devices[].coverage[]` 直接给了每台基站的累计帧数：

```json
"coverage":[{"base":"A101B835","frames":1234,"sensors":5}], "basesSeen":3
```

脚本骨架：

```python
import json, socket, time, csv

sock = socket.create_connection(('127.0.0.1', 36840), timeout=5)
f = sock.makefile('r', encoding='utf-8', newline='\n')

prev, t0, out = {}, None, []
while True:
    line = f.readline()
    if not line:
        break
    try:
        j = json.loads(line)
    except ValueError:
        continue
    if j.get('type') != 'state':
        continue
    if t0 is None:
        t0 = j['t']
    for d in j.get('devices', []):
        if not d.get('connected'):
            continue
        for c in d.get('coverage', []):
            k = (d['serial'], c['base'])
            old = prev.get(k)
            prev[k] = c['frames']
            if old is not None and j['t'] - t0 > 2:
                out.append((j['t'] - t0, d['serial'], c['base'],
                            c['frames'] - old, c['sensors'], d.get('basesSeen')))
    if j['t'] - t0 > 900:            # 15 分钟
        break

with open('coverage.csv', 'w', newline='') as fh:
    w = csv.writer(fh)
    w.writerow(['t', 'device', 'base', 'd_frames', 'sensors', 'bases_seen'])
    w.writerows(out)
```

输出 `coverage.csv`，`d_frames` 是每个 50 ms 采样周期内的新增帧数，累加除以时间就是帧率。

### 0.3 采集（1 小时）

- [ ] 开 SteamVR + openhouse，正常玩 10–15 分钟
- [ ] 期间刻意做几件事：转身背对、蹲下、走到房间角落、把手柄放到身后
- [ ] 记下这几段时间点（后面分析时单独看）

### 0.4 分析（1 小时）

算两个东西：

**（a）稳定期矩阵**：去掉刻意遮挡的时段，每台设备 × 每台基站的帧率。

```
设备 \ 基站      A      B      C      D    | basesSeen
HMD            15.0   15.0   15.0   15.0   |   4
左控制器        15.0   14.8   14.9    0     |   3
右脚 tracker     0     0     0    15.0    |   1
```

**（b）最差时段矩阵**：只看"刻意遮挡"那几分钟。

---

### Gate A · 值不值得做

| 观察结果 | 结论 | 下一步 |
|---|---|---|
| 所有设备 `basesSeen` ≥ 3，且各基站的帧率接近 | **停** | 等权已接近最优，收益 < 10%。去做基站摆位优化，那个免费 |
| 有设备稳定 `basesSeen` = 2，且帧率明显偏低 | 值得 | 进阶段 1，预期最差设备 +50% |
| 有设备稳定 `basesSeen` = 1 | **很值得** | 进阶段 1，预期最差设备 +100–180% |
| 有某台基站对**所有**设备帧率都接近 0 | 值得 | 进阶段 1，kill-switch 就能拿到 +33% |

**如果结果落在第一行，直接结束，不要往下。** 这不是失败，是省下两周。

> 同时检查：如果某台基站的帧率低是因为**残差差**（而不是看不到），那是标定问题，
> 不是调度问题。查 `openhouse-viewer` 里这台基站的 `faults` 和 `observations`，
> 必要时用 `Reset room` 重学，或者恢复它的工厂标定。

---

## 阶段 1 · 工具链（0 改动，半天）

### 目的

确认你**能把这个固件编译出来**。编不出来，后面全是空谈。

### 1.1 环境

推荐 WSL（Ubuntu），跟作者的流程一致：

```bash
sudo apt install build-essential gcc-arm-none-eabi python3 git
```

### 1.2 编译原始固件

```bash
cd openhouse
make -C basestation_firmware clean
make -C basestation_firmware
```

应该产出 `basestation_firmware/build/firmware.bin`。

### 1.3 跑主站仿真测试

```bash
make -C basestation_firmware hosttest
```

这会编 4 份 `multi.c` 成 `.so`，用 8 个随机种子跑 30 分钟的上下电模拟。**必须全过**。

### Gate B · 能不能编译

- [x] `firmware.bin` 生成成功
- [x] 向量校验通过（`lpcsum.py check` 在 Makefile 里自动跑）
- [x] `hosttest` 8 个种子全过

三条都过才能改代码。任何一条不过，先解决工具链，不要动源码。

### 1.4 存档

```bash
cp basestation_firmware/build/firmware.bin ~/lhbs-baseline-$(date +%F).bin
```

---

## 阶段 2 · 固件开关（改固件，1 天）

### 目的

打通 **USB → 主站 → OOTX → 从站** 这条链路。不做算法，只做通路。

### 2.1 加命令

`basestation_firmware/src/console.c`：

```
multi mask <hex>    覆盖主站自动算出的轮班 mask（仅主站有效）
multi mask auto     恢复自动
```

存進 `multi_cfg_t` 的新字段 `mask_override`（reserved1 还有 108 字节空闲）。

### 2.2 改判定

`multi.c` 的 `multi_sweeps_in()` 里，`mask_live` 的来源从"主站统计"改成
"有 override 用 override，否则用统计值"。

### 2.3 看门狗

```
如果超过 5 秒没收到新的 mask 命令 → 自动回退到 auto
```

**这条必须有。** 没有它，驱动崩了会留下一个僵死的 mask，整房间停摆。

### 2.4 刷机

- [ ] **先把四台的 flash 全备份出来**，复制到别的盘
- [ ] 先刷**一台**，设成 1 号，实测
- [ ] 用 `multi` 命令读回，确认 override 生效
- [ ] 确认断电重启后 override 被清掉（默认 auto）

### Gate C · 链路通不通

- [ ] 电脑能打开该基站的 COM 口
- [ ] 能用 `get sys.name` 读到序列号
- [ ] `multi mask 0x7` 之后，`multi` 显示只有 3 台在轮
- [ ] 四台基站都能被同一张 mask 驱动（从站通过 OOTX 收到）

---

## 阶段 3 · 驱动链路（改驱动，2–3 天）

### 目的

让驱动自动说话，人不用敲命令。

### 3.1 串口层

- [ ] 枚举 COM 口，逐个发 `get sys.name`，按序列号建映射
- [ ] 断线重连（USB 拔了/基站掉电）
- [ ] 心跳：每 1 秒一条无副作用的命令（比如 `status`），兼作看门狗

### 3.2 控制逻辑（最笨版本）

```
对每台基站 s：
    如果 连续 10 秒 coverage[s] 对所有设备都是 0
        把它从 mask 里摘掉
    否则
        放回去
```

防止"摘掉就永远回不来"：摘掉的基站每 10 秒放回去扫一转，重新测一次。

### Gate D · 能不能自动控制

- [ ] 拔掉某台基站的电源，10 秒内驱动自动把它从 mask 摘掉（或保持，取决于实现）
- [ ] 把它放回去，10 秒内自动恢复
- [ ] 杀掉驱动，5 秒内基站回到 auto 模式
- [ ] 这一整套跑 30 分钟不崩

**Gate D 过了，阶段 4 才有意义。** 因为阶段 4 只是把"二值"换成"权重"，通路是同一套。

---

## 阶段 4 · 权重调度（改两者，1 周）

### 4.1 固件

把 mask（4 bit）扩展成播放表（16 格 × 2 bit = 4 字节）：

```c
const uint8_t slot = (playlist[(rev & 15) >> 2] >> (((rev & 3) ^ 3) * 2)) & 3;
bool mine = (slot == MC->slot);
```

OOTX 载荷从 34 字节扩到 39 字节（从站解码器限制 40，刚好卡住，不用改）。

切换时机沿用现有的 `multi_superframe()` 机制：帧广播完成后，所有基站在同一超帧边界翻。

### 4.2 驱动

```
每 10 秒：
  1. 从遥测算 V[d][s]（10 秒滑动窗口，取 coverage 的帧率占比）
  2. 迭代 5 次：
       r_d = Σ V[d][s] · w_s
       g_d = max(0, T − r_d)          T = 0.75 (45 Hz)
       demand[s] = Σ_d g_d / |S_d|
       w_s = ε + k · demand[s]        ε = 0.1
  3. 展开成 16 格播放表（Bresenham 均匀铺开）
  4. 变化超过 ±0.05 才下发
```

### 4.3 必须同时做的保护

- [ ] 迟滞：缺口持续 > 2 秒才改，单次变化 ≤ 0.05
- [ ] 探测：w 不能低于 ε，保证每台基站都还有机会被重新评估
- [ ] 健康检查：某台基站残差显著差于其他三台 → **报警，不降权**

---

### Gate E · 有没有真实提升

**方法：同路线 A/B 对照。**

```
1. 基线：当前固件 + 等权，录一段完整使用（10 分钟）
   SteamVR 设置里开 Record Session，或 LH_CAPTURE=D:\cap-baseline

2. 改：刷新固件 + 开权重

3. 对照：同样路线、同样设备，录 10 分钟到 D:\cap-weighted

4. 打分：
   lhtool session D:\cap-xxx --room roommap_copy.json --trace out.jsonl
   tools/eval_replay.sh ...
```

看四个数：

| 指标 | 期望 |
|---|---|
| `valid%` | 上升 |
| `coast%` | 下降 |
| `lost%` | 下降（这是主要目标） |
| `jitter` | 不变或略升（可接受） |

**验收线：`lost%` 相对下降 ≥ 30%，`valid%` 不下降。**

达不到就回退——把 `multi mask auto` 设回去，一行命令。

---

## 阶段 5 · 收敛（持续）

- [ ] 每天使用后看一眼 `coverage.csv`，确认权重没有漂
- [ ] 移动基站后手动 `Reset room`，并让驱动重新学 10 秒
- [ ] 记录每个固件/驱动版本的 A/B 数据

---

## 风险登记

| # | 风险 | 应对 |
|---|---|---|
| R1 | 上游更新导致合并冲突 | 优先做上游 PR；本地保留分支，每次同步前打 tag |
| R2 | 刷固件后基站红闪（激光电流棘轮） | 先备份 flash；红闪时按 `ai/firmware/MULTISTATION.md` 与 issue #1/#6 处理 |
| R3 | 权重优化掉"标定坏的基站"，掩盖问题 | 健康检查优先于降权，残差异常先报警 |
| R4 | 驱动崩溃导致整房间停摆 | 固件看门狗，5 秒无心跳回 auto |
| R5 | 可见性测量本身受调度影响（测不到就没法评） | w 下限 ε = 0.1，保留探测预算 |
| R6 | 主站 USB 线长度限制（5 m） | 主站选离电脑近的那台；否则用有源延长线 |

---

## 每天的第一件事

```powershell
cd J:\workspace_AI_Agent\baibaizi\research\openhouse
git status -sb
git diff --stat
```

改代码之前先确认工作区干净，改完立刻 `git commit`。这是一周的活，不是一天的活——**每次能回退，比每次写对更重要。**
