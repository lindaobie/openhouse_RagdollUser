# 原厂固件还原工具（Windows）
#
# 把基站刷回刷机前的原厂固件。
#
#   Restore-Stock.bat                    列出 backups\ 里的镜像，选一个还原
#   Restore-Stock.bat -Backup 文件.bin   直接指定文件
#
# 写入之前会读一遍基站里现有的固件，如果已经一致就直接跳过。
param([string]$Backup = "")

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$label = "CRP DISABLD"
$folder = Join-Path $here "backups"

function Fail([string]$text) {
  Write-Host ""
  Write-Host "错误：$text" -ForegroundColor Red
  exit 1
}

function Test-Vectors([byte[]]$bytes) {
  if ($bytes.Length -lt 32) { return $false }
  [uint64]$sum = 0
  for ($i = 0; $i -lt 8; $i++) {
    $sum = ($sum + [BitConverter]::ToUInt32($bytes, $i * 4)) % 4294967296
  }
  return $sum -eq 0
}

# 判断一个镜像是什么：原厂（带 20xx-xx-xx 构建串、没有 lhbs 标记）还是 lhbs
function Describe([byte[]]$bytes) {
  $head = ""
  $n = [Math]::Min(65536, $bytes.Length)
  if ($n -gt 0) { $head = [Text.Encoding]::ASCII.GetString($bytes, 0, $n) }
  if ($head -match 'lhbs [A-Z][a-z]{2} ') {
    $m = [regex]::Match($head, 'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d')
    $when = if ($m.Success) { $m.Value } else { "" }
    return @{ Kind = "lhbs 固件"; Detail = $when }
  }
  $m = [regex]::Match($head, '20\d\d-\d\d-\d\d \d\d:\d\d:\d\d')
  if ($m.Success) { return @{ Kind = "原厂镜像"; Detail = $m.Value } }
  return @{ Kind = "未知镜像"; Detail = "" }
}

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

Write-Host "=================================================================="
Write-Host "  原厂固件还原工具"
Write-Host "=================================================================="
Write-Host ""

# ---- 选镜像 ----
if ($Backup) {
  if (-not (Test-Path -LiteralPath $Backup) -and -not [IO.Path]::IsPathRooted($Backup)) {
    $alt = Join-Path $here $Backup
    if (Test-Path $alt) { $Backup = $alt }
  }
  if (-not (Test-Path -LiteralPath $Backup)) { Fail "找不到文件：$Backup" }
  $image = (Resolve-Path -LiteralPath $Backup).Path
} else {
  if (-not (Test-Path $folder)) {
    Write-Host ""
    Write-Host "  还没有任何备份。" -ForegroundColor Yellow
    Write-Host "  backups 目录在第一次刷固件时会自动创建，里面存的是每台基站的原厂镜像。"
    Write-Host "  如果这是新解压的包，直接先刷固件就行。"
    Write-Host ""
    exit 1
  }
  $files = @(Get-ChildItem -Path $folder -Filter *.bin -File | Sort-Object LastWriteTime)
  if ($files.Count -eq 0) { Fail "backups 目录里没有任何 .bin 镜像" }

  Write-Host "backups 里可用的镜像："
  Write-Host ""
  Write-Host ("  {0,-3} {1,-40} {2,-10} {3}" -f "序号", "文件", "类型", "构建时间")
  Write-Host "  " + ("-" * 84)
  $i = 0
  foreach ($f in $files) {
    $i++
    $bytes = [IO.File]::ReadAllBytes($f.FullName)
    $d = Describe $bytes
    $ok = if (Test-Vectors $bytes) { "" } else { "  [校验异常]" }
    Write-Host ("  {0,-3} {1,-40} {2,-10} {3}{4}" -f $i, $f.Name, $d.Kind, $d.Detail, $ok)
  }
  Write-Host ""

  $pick = Read-Host "要还原哪一个？输入序号（q 退出）"
  if ($pick -match '^[Qq]') { exit 0 }
  if ($pick -notmatch '^\d+$' -or [int]$pick -lt 1 -or [int]$pick -gt $files.Count) { Fail "序号无效" }
  $image = $files[[int]$pick - 1].FullName
}

if (-not (Test-Path -LiteralPath $image)) { Fail "找不到文件：$image" }
$imageBytes = [IO.File]::ReadAllBytes($image)

if (-not (Test-Vectors $imageBytes)) { Fail "$image 不是有效镜像（向量校验不通过），拒绝写入" }
if ($imageBytes.Length -gt 131072) { Fail "镜像超过 128 KB，不是基站固件" }

$desc = Describe $imageBytes
Write-Host ""
Write-Host "已选择：$(Split-Path -Leaf $image)"
Write-Host "  类型：$($desc.Kind)  $($desc.Detail)"
Write-Host ""

if ($desc.Kind -ne "原厂镜像") {
  Write-Host "注意：这不是原厂镜像。还原之后基站跑的还是 lhbs，不是出厂状态。" -ForegroundColor Yellow
  $go = Read-Host "确定要继续吗？[y/N]"
  if ($go -notmatch '^[Yy]') { exit 0 }
}

# ---- 接基站 ----
Write-Host ""
Write-Host "--------------------------------------------------------------"
Write-Host "  操作步骤：先别插任何线，按下面顺序来"
Write-Host "--------------------------------------------------------------"
Write-Host ""
Show-PlugSteps
Write-Host ""
Write-Host "  做完之后，资源管理器里会出现一块叫 CRP DISABLD 的盘。"
Write-Host "  如果 Windows 说这块盘需要格式化，直接关掉，千万别点。" -ForegroundColor Yellow
Write-Host ""

$drive = Wait-Drive
Write-Host "已找到基站（盘符 $drive）。"
Write-Host ""

# ---- 读一下现在是什么（只用于判断是否已经一致，不存文件）----
$current = [IO.File]::ReadAllBytes("$drive\firmware.bin")
if ($current.Length -eq 0) { Fail "读不到当前固件" }

if (Same $imageBytes $current $imageBytes.Length) {
  Write-Host "基站里已经是这个镜像了，不需要还原。" -ForegroundColor Green
  Eject $drive
  exit 0
}

# ---- 确认 ----
Write-Host "--------------------------------------------------------------"
Write-Host "  即将写入：请核对下面的文件名"
Write-Host "--------------------------------------------------------------"
Write-Host "    文件    $(Split-Path -Leaf $image)"
Write-Host "    类型    $($desc.Kind)  $($desc.Detail)"
Write-Host "    大小    $($imageBytes.Length) 字节"
Write-Host "    校验    $((Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash.Substring(0,16))..."
Write-Host "    目标    $drive\firmware.bin"
Write-Host "--------------------------------------------------------------"
Write-Host ""
$answer = Read-Host "现在还原吗？[y/N]"
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
Write-Host "等待你拔掉基站……"
$need = 4
$absent = 0
while ($absent -lt $need) {
  if (Find-Drive) { $absent = 0 } else { $absent++ }
  Start-Sleep -Milliseconds 500
}
Write-Host "已检测到拔出。"
$drive = Wait-Drive
$check = [IO.File]::ReadAllBytes("$drive\firmware.bin")
Eject $drive

if (Same $imageBytes $check $imageBytes.Length) {
  Write-Host ""
  Write-Host "校验通过：基站已经回到这个镜像。" -ForegroundColor Green
  Write-Host "拔下基站，正常插上电源（不要按按钮）。"
  Write-Host ""
  Write-Host "提醒：如果还原的是原厂固件，开机后按背面按钮确认频道回到 A / b / c。"
  Write-Host "      原厂固件用不了 1-4 那几个多站频道。"
  exit 0
}

Write-Host ""
Write-Host "校验失败：基站里的固件和刚才写入的不一致。" -ForegroundColor Red
Write-Host "可以再运行一次；如果反复失败，换一根数据线或换一个 USB 口。"
exit 2
