# lhbs 基站固件刷写工具（Windows）
#
#   Install-Firmware.bat                      自动备份当前固件，然后刷入
#   Install-Firmware.bat -Restore 备份.bin    把备份刷回去
param([string]$Restore = "", [switch]$Verify)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$label = "CRP DISABLD"

function Fail([string]$text) {
  Write-Host ""
  Write-Host "错误：$text" -ForegroundColor Red
  exit 1
}

# 选择要写入的镜像
if ($Restore) {
  $image = $Restore
} else {
  $found = Get-ChildItem -Path (Join-Path $here "firmware") -Filter *.bin -ErrorAction SilentlyContinue |
           Select-Object -First 1
  if (-not $found) { Fail "在 $here\firmware 里没有找到固件文件" }
  $image = $found.FullName
}
if (-not (Test-Path $image)) { Fail "找不到文件：$image" }
$imageBytes = [IO.File]::ReadAllBytes((Resolve-Path $image))

# LPC11U37 只肯启动前 8 个字加起来等于 0 的镜像
function Test-Vectors([byte[]]$bytes) {
  if ($bytes.Length -lt 32) { return $false }
  [uint64]$sum = 0
  for ($i = 0; $i -lt 8; $i++) {
    $sum = ($sum + [BitConverter]::ToUInt32($bytes, $i * 4)) % 4294967296
  }
  return $sum -eq 0
}
if (-not (Test-Vectors $imageBytes)) { Fail "$image 不是有效的固件镜像（向量校验不通过）" }

# 0x2FC 处如果有代码读保护字，会把 USB 引导程序永久锁死，这种镜像绝不写入
if ($imageBytes.Length -ge 0x300) {
  $crp = [BitConverter]::ToUInt32($imageBytes, 0x2FC)
  if (@(0x4E697370, 0x12345678, 0x87654321, 0x43218765) -contains $crp) {
    Fail "这个镜像会把基站的引导程序锁死，拒绝写入"
  }
}
if ($imageBytes.Length -gt 131072) { Fail "镜像超过了基站 128 KB 的 flash 容量" }

function Find-Drive {
  $disk = Get-CimInstance Win32_LogicalDisk | Where-Object { $_.VolumeName -eq $label } | Select-Object -First 1
  if ($disk -and (Test-Path "$($disk.DeviceID)\firmware.bin")) { return $disk.DeviceID }
  return $null
}

function Show-PlugSteps {
  Write-Host "  1. 拔掉基站的电源线"
  Write-Host "  2. 拔掉基站的 micro-USB 线"
  Write-Host "  3. 按住基站背面按钮（一直按住！），插上 micro-USB 线"
  Write-Host "  4. 插上电源线"
  Write-Host "  5. 松开基站背面按钮"
}

function Wait-Drive {
  $drive = Find-Drive
  if (-not $drive) {
    Write-Host "正在等待基站出现……"
    while (-not $drive) {
      Start-Sleep -Seconds 1
      $drive = Find-Drive
    }
  }
  return $drive
}

# 等真实的拔出：必须连续 2 秒看不到盘，避免一次查询抖动或系统自动重挂就放行
function Wait-Unplug {
  Write-Host "等待你拔掉基站……"
  $need = 4
  $absent = 0
  while ($absent -lt $need) {
    if (Find-Drive) { $absent = 0 } else { $absent++ }
    Start-Sleep -Milliseconds 500
  }
  Write-Host "已检测到拔出。"
}

function Eject([string]$drive) {
  try {
    (New-Object -ComObject Shell.Application).Namespace(17).ParseName($drive).InvokeVerb("Eject")
  } catch { }
  Start-Sleep -Seconds 2
}

function Same([byte[]]$a, [byte[]]$b, [int]$count) {
  if ($a.Length -lt $count -or $b.Length -lt $count) { return $false }
  for ($i = 0; $i -lt $count; $i++) {
    if ($a[$i] -ne $b[$i]) { return $false }
  }
  return $true
}

# 原地写入、直接落到设备上。把文件拖进资源管理器那种做法会替换掉它，
# 那会把 flash 写乱；这一段不会。
function Write-Image([byte[]]$bytes, [string]$drive) {
  $size = [int]([math]::Ceiling($bytes.Length / 512) * 512)
  $padded = New-Object byte[] $size
  for ($i = 0; $i -lt $size; $i++) { $padded[$i] = 0xFF }
  [Array]::Copy($bytes, $padded, $bytes.Length)
  $stream = New-Object IO.FileStream("$drive\firmware.bin", [IO.FileMode]::Open, [IO.FileAccess]::Write,
                                     [IO.FileShare]::None, 512, [IO.FileOptions]::WriteThrough)
  try {
    $stream.Write($padded, 0, $padded.Length)
    $stream.Flush($true)
  } finally {
    $stream.Close()
  }
}

Write-Host "================================================================"
Write-Host "  lhbs 基站固件刷写工具"
Write-Host "================================================================"
Write-Host ""
if ($Verify) {
  Write-Host "只做校验，不写入：$(Split-Path -Leaf $image)"
} elseif ($Restore) {
  Write-Host "将要把这个备份刷回基站：$(Split-Path -Leaf $image)"
} else {
  Write-Host "将要刷入的固件：$(Split-Path -Leaf $image)"
}
Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host "  接下来会发生什么"
Write-Host "--------------------------------------------------------------"
Write-Host ""
Write-Host "  接上基站后，计算机会把它认成一块很小的 U 盘（128 KB）。"
if ($Verify) {
  Write-Host "  脚本只读取那块盘，不写任何东西。"
} else {
  Write-Host "  脚本通过那块盘把固件写进去，写入本身只要几秒。"
}
Write-Host ""
Write-Host "  需要准备："
Write-Host "    · 一根 micro-USB 数据线（充电线不行，必须能传数据）"
Write-Host "    · 基站的电源适配器"
Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host "  操作步骤：先别插任何线，按下面顺序来"
Write-Host "--------------------------------------------------------------"
Write-Host ""
Show-PlugSteps
Write-Host ""
Write-Host "  做完上面几步，资源管理器里会出现一块叫 CRP DISABLD 的盘。"
Write-Host "  如果 Windows 弹窗说这块盘需要格式化，直接关掉，千万别点。" -ForegroundColor Yellow
Write-Host ""

$drive = Wait-Drive
Write-Host "已找到基站（盘符 $drive）。"
Write-Host ""

# -Verify：只读、只比对，不备份也不写入
if ($Verify) {
  $check = [IO.File]::ReadAllBytes("$drive\firmware.bin")
  Eject $drive
  if ($check.Length -ge 0x1F058 -and [Text.Encoding]::ASCII.GetString($check, 0x1F000, 8) -eq 'LHEEDUMP') {
    $sn = ([Text.Encoding]::ASCII.GetString($check, 0x1F050, 16) -split "`0")[0]
    if ($sn -match '^[0-9A-Fa-f]{8}$') { Write-Host "这台基站的序列号：LHB-$($sn.ToUpper())" -ForegroundColor Cyan }
  }
  if (Same $imageBytes $check $imageBytes.Length) {
    Write-Host "校验通过：基站里的固件与此镜像一致。" -ForegroundColor Green
    exit 0
  }
  Write-Host "不一致：基站里的固件不是这个镜像。" -ForegroundColor Red
  exit 2
}

$backup = ""
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

if (-not $Restore) {
  $folder = Join-Path $here "backups"
  New-Item -ItemType Directory -Force -Path $folder | Out-Null
  $backup = Join-Path $folder "firmware-$stamp.bin"

  Write-Host "正在读取并备份当前固件……"
  $current = [IO.File]::ReadAllBytes("$drive\firmware.bin")
  if ($current.Length -eq 0) { Fail "读不到当前固件" }
  [IO.File]::WriteAllBytes($backup, $current)
  Write-Host "已备份到：backups\firmware-$stamp.bin" -ForegroundColor Green

  # 如果这台之前已经刷过 lhbs，提醒一句（备份的就不是原厂镜像了）
  $head = [Text.Encoding]::ASCII.GetString($current, 0, [Math]::Min(65536, $current.Length))
  if ($head -match 'lhbs [A-Z][a-z]{2} ') {
    Write-Host ""
    Write-Host "注意：这台基站上已经是 lhbs 固件，不是原厂。这次备份存的是 lhbs。" -ForegroundColor Yellow
  }

  Write-Host ""
  Write-Host "================================================================" -ForegroundColor Yellow
  Write-Host "  警告：这串编号必须贴在机身上，否则四台基站分不清" -ForegroundColor Yellow
  Write-Host "================================================================" -ForegroundColor Yellow
  Write-Host ""
  Write-Host ("   本机编号： " + $stamp) -ForegroundColor Black -BackgroundColor Yellow
  Write-Host ""
  Write-Host "  把上面这串号写在标签上，贴到这台基站的机身。"
  Write-Host "  四台基站长得一模一样，没有标签，过两天就分不清谁是谁。"
  Write-Host "  没有标签纸就写在胶带上，或者拍照记下贴在机身上的位置。"
  Write-Host ""
  Write-Host "  这一步不做，将来出问题只能靠猜。" -ForegroundColor Yellow
  Write-Host "================================================================" -ForegroundColor Yellow
  Write-Host ""

  if (Same $imageBytes $current $imageBytes.Length) {
    Write-Host "这台基站已经是这个固件了，不需要再写。" -ForegroundColor Green
    Eject $drive
    exit 0
  }
}

Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host "  即将写入：请核对下面的文件名"
Write-Host "--------------------------------------------------------------"
Write-Host "    文件    $(Split-Path -Leaf $image)"
Write-Host "    大小    $($imageBytes.Length) 字节"
Write-Host "    校验    $((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.Substring(0,16))..."
Write-Host "    目标    $drive\firmware.bin"
Write-Host "--------------------------------------------------------------"
Write-Host ""
$answer = Read-Host "现在写入吗？[y/N]"
if ($answer -notmatch '^[Yy]') {
  Write-Host "没有写入。"
  Eject $drive
  exit 0
}

Write-Image $imageBytes $drive
Eject $drive
Write-Host "已写入。"
Write-Host ""
Write-Host "现在做校验。"
Write-Host ""
Write-Host "  校验必须靠一次真正的断电重插：只有重插之后，读回来的才是 flash 里" -ForegroundColor Yellow
Write-Host "  真实的内容，而不是 Windows 缓存里的。线不拔，校验等于没做。" -ForegroundColor Yellow
Write-Host ""
Write-Host "请按流程操作："
Show-PlugSteps
Write-Host ""
Wait-Unplug
$drive = Wait-Drive
$check = [IO.File]::ReadAllBytes("$drive\firmware.bin")
Eject $drive

if (Same $imageBytes $check $imageBytes.Length) {
  Write-Host ""
  Write-Host "校验通过。" -ForegroundColor Green
  Write-Host "拔下基站，正常插上电源（不要按按钮），它就会用新固件启动。"
  exit 0
}

Write-Host ""
Write-Host "校验失败：基站里的固件和刚才写入的不一致。" -ForegroundColor Red
if ($backup) {
  Write-Host "可以再运行一次，或者用备份刷回去："
  Write-Host "  Install-Firmware.bat -Restore `"backups\firmware-$stamp.bin`""
}
exit 2
