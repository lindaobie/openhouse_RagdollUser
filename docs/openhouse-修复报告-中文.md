# 一批一代基站刷完 lhbs 后红灯常闪、不出光：EEPROM 里的激光电流是坏值

**组件：** `basestation_firmware`（lhbs）
**对照版本：** openhouse 1.0.1 / 固件 `lhbs-20261108`（构建 2026-11-08 20:15:40）
**硬件：** 1.0 基站（LPC11U37），跨生产批次

---

## 一句话

有些一代基站的 EEPROM **用户分区**里存着 `laser.0.current = 1`、`laser.1.current = 1`
（工厂分区还是真实值，比如 127 / 112）。这些机器在原厂固件上一切正常，刷成 lhbs 之后
**当场故障**：红灯常闪、激光完全不出、对追踪零贡献。

原因在功率环的安全窗口——它锚定的是**存的**电流（`boot_current ± 60`，上限 230）。
`boot_current = 1` 时窗口只有 `[0, 61]`，而那颗二极管要跑到配置的 30 mW 大概需要 127，
永远够不着，于是功率环认定"功率检测坏了"，锁死 `laser_fault`。

三行就能修。

---

## 现象

刷完 lhbs、切到多站频道（`1`–`4`，别的频道也一样）之后，部分机器：

- 状态灯**红色常闪**（`P.sys_faults = 0x11`，即激光故障）
- 转子正常转（有声音，控制台里 `locked`）
- **激光永远不亮**，所以什么都追踪不到

同一批机器在原厂固件上完全正常；插上 lhbs 只要 EEPROM 里存的电流还算正常就不会出事。

同一天、同一个镜像刷了六台，**四台故障、两台正常**——所以不是刷机失误，也不是随机。

---

## 根因

### 功率环把电流卡在 `boot_current + 60`

`laser.c` 的 `apc_sample()`：

```c
float target = n ? P.laser_1_pwr : P.laser_0_pwr;
uint8_t *cur = n ? &P.laser_1_current : &P.laser_0_current;
int hi = boot_current[n] + 60, lo = boot_current[n] - 60;
if (hi > 230) hi = 230;
if (target - pw > 1.0f) {                   /* 光不够 */
    if (*cur >= hi) {                       /* 电流也加到头了 */
        laser_fault = true;                 /* 红灯，关激光 */
        lasers_off();
        return;
    }
    (*cur)++;
}
```

而 `boot_current` 是在 `laser_init()` 里从 EEPROM 读进来的：

```c
boot_current[0] = P.laser_0_current;
boot_current[1] = P.laser_1_current;
```

所以这个窗口是**相对 EEPROM 里那个值**的，不是一个绝对范围。

### 那个 `1` 是怎么进去的

原厂固件在**切频道会保存整个参数块**。保存的时候，`laser.*.current` 里装的是功率调节
**当时停在**的值，不是开机时的值。反复保存就会把这个数一路**棘轮**往下带，而工厂分区里
永远留着真正的数。

这个棘轮效应仓库里已经记录过（`laser_boot_currents_for_save()`，2026-11-08 加的）：
lhbs 现在保存的是开机电流而不是调节后的值，所以它**不会再制造**这种状态。
但它**不会修复**已经带病的机器——而红灯的正是这一批。

### 为什么原厂固件没事

原厂的功率环没有这个天花板：它一路把电流往上推直到功率达标，不管存储值是多少。
存的 `1` 对它只是个起点。lhbs 从同一个起点出发，但走到 `boot_current + 60` 就停了。

这大概率也解释了
[#1 "Need to flash twice"](https://github.com/YusufOzmen01/openhouse/issues/1)：
刷回原厂、开机跑一次、再刷 lhbs 就好了——跑原厂那一次，它的功率环找到了正确的电流，
下一次保存把它写了回去。

---

## 证据

通过 ISP 盘把 EEPROM 读出来：flash sector 31（`0x1F000`）是 `LHEEDUMP` 头，
`+0x40` 开始是 4032 字节的原始 EEPROM。每个分区是
`{"CPRM", u32 version, u32 crc32}` + 551 字节 payload，payload 是存的参数按表顺序拼接。

**一台故障机**（用户分区 vs 工厂分区）：

| 参数 | 用户 `0x000` | 用户 `0x300` | 工厂 `0x600` |
|---|---|---|---|
| `laser.0.current` | **1** | **1** | 127 |
| `laser.1.current` | **1** | **1** | 112 |
| `laser.0.pwr` | 30.0 | 30.0 | 30.0 |
| `laser.0.pwr.b` | 7.0851 | 7.0851 | 7.0851 |
| `laser.0.pwr.m` | 0.09106 | 0.09106 | 0.09106 |
| `laser.0.pwr.gain` | 4 | 4 | 4 |
| `rotor.0.pwm` | 2017 | 2022 | 2096 |

**只有那两个电流不一样，其余标定常数和工厂分区逐字节相同。** 三个分区 CRC 全部有效，
所以这不是 EEPROM 损坏——是一次**格式完全正确的坏值保存**。

把 183 个存储参数全部比了一遍，用户分区和工厂分区只差 6 个：两个电流、
`timebase.mode` 和 `mode.current`（频道状态）、以及两个自动调谐的 `rotor.*.pwm`。
其余全部一致。

**同一批刷机里一台正常的机器**，作对照：

| 参数 | 用户 `0x000` | 用户 `0x300` | 工厂 `0x600` |
|---|---|---|---|
| `laser.0.current` | 138 | 138 | 147 |
| `laser.1.current` | 125 | 125 | 141 |

健康区间参考（`apc_sample()` 里的注释）：原厂默认 190，正常机器跑在 117–172。

---

## 修复

`basestation_firmware/src/laser.c` 的 `laser_init()`，在把电流写进 ISL58303 之前：

```diff
     /* stock laser_update_task: bias and drive current from the parameters */
+    /* Some units carry laser.0/1.current = 1 in their EEPROM user partitions: an older
+     * firmware's channel-change saves wrote the power-regulated value back, so the
+     * stored current ratcheted down while the factory partition still holds the real
+     * one (one unit here: user 1/1, factory 127/112). Stock's power loop has no ceiling
+     * and copes; ours caps at boot_current + 60, so such a base cannot reach 30 mW, is
+     * declared faulted and turns into a red-blinking station that never emits.
+     * A current below 40 cannot be a working setting (healthy units run 117-172 and the
+     * stock default is 190), so treat it as missing and use the default. The value is
+     * written back to the EEPROM by the next channel change, which heals the unit. */
+    if (P.laser_0_current < 40) P.laser_0_current = 190;
+    if (P.laser_1_current < 40) P.laser_1_current = 190;
     ok &= isl_channel_reg(isl_channel[0], 0, P.laser_0_bias);
```

`boot_current = 190` 之后窗口变成 `[130, 230]`，功率环自己就能找到需要的电流。

触发条件是"小于 40"，而正常机器存的值都在 117 以上，所以**健康机器完全不受影响**，
那段分支根本不会进。

修复是**永久的**：下一次切频道时 `laser_boot_currents_for_save(true)` 会把
`boot_current`（现在已经是 190）写回 EEPROM。

### 已经红灯的机器怎么救

已经在旧固件上跑出红灯的机器，EEPROM 里还是那个坏值，所以**不要直接重刷新固件**。
已验证的唯一流程，顺序不能换：

1. **先把这台的原厂镜像刷回去**
   `Restore-Stock.bat -Backup backups\firmware-<时间戳>.bin`
   不带参数双击也行，它会列出 `backups\` 里所有镜像让你选序号
2. **正常上电，让它跑几秒，按平时的方式把频道设好。**
3. **再刷新固件：** 双击 `Install-Firmware.bat`

**只推荐这一条路径。** 串口改参数之类的做法没有验证过，不推荐——
方案越多，变数越多。

---

## 验证

六台 1.0 基站，刷之前全部在原厂固件上正常工作，同一天用同一个镜像刷入：

| 分组 | 存储的电流 | 1.0.1 上的表现 | 按上面流程修复后 |
|---|---|---|---|
| 六台中的四台 | 低于 40（其中一台直接读出：用户分区 `1` / `1`，工厂 `127` / `112`） | 红闪，不出光 | **绿灯，正常追踪** |
| 六台中的两台 | 在健康区间 | 正常 | 没有重刷 |

补丁只在存储值低于 40 时生效，所以行为发生变化的这四台**必然**都低于 40——
这条推理不依赖把每一台都拆开读。

镜像由 `arm-none-eabi-gcc 13.2.1` 从本仓库编译，向量校验通过（`lpcsum.py check`），
通过 ISP 盘原地写入，断电重插后重新读取比对，与镜像逐字节一致。

单台验证是在**本来就故障的**机器上做的，所以即使出现回归也不可能把一台好机器弄坏。

---

## 怎么自查

1. 断电，按住背面按钮，把 micro-USB 插到电脑，再插电源，松手。
   会出现一块叫 `CRP DISABLD` 的盘，里面一个 128 KB 的文件。
2. 那个文件里：`0x1F000` 是 `LHEEDUMP` 头，`0x1F040` 开始是 EEPROM。
3. 第 n 个分区从 `0x1F040 + n*0x300` 开始，跳过 12 字节头，后面就是按表顺序拼接的
   参数。`sys.name` 在 payload 偏移 4，两个激光电流在 98 和 124（由 `params_table.h` 固定）。

那两个值是 1 或接近 1 的机器，在任何没打这个补丁的 lhbs 上都会故障。

---

## 关联

- `#1 Need to flash twice` —— 大概率是同一个根因，从另一面看到的现象。
- `#6` —— 那台靠把电流改回工厂值救回来的机器，是同一个故障，只是改在二进制里而不是源码里。
