#!/usr/bin/env python3
"""Package the lhbs base station firmware with its Linux and Windows installers.

usage: package-firmware.py   (builds basestation_firmware first; needs arm-none-eabi-gcc)
Output: dist/openhouse-basestation-firmware/ and dist/openhouse-basestation-firmware.zip
"""
import datetime
import shutil
import stat
import subprocess
import zipfile
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
fw = repo / 'basestation_firmware'
subprocess.run(['make', '-s', '-C', str(fw)], check=True)
image = fw / 'build/firmware.bin'
data = image.read_bytes()
if sum(int.from_bytes(data[i:i + 4], 'little') for i in range(0, 32, 4)) % 2**32:
    raise SystemExit('built image has a bad vector checksum')

commit = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', '--short', 'HEAD'], text=True).strip()
dirty = bool(subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain', '--', 'basestation_firmware'], text=True).strip())
name = f"lhbs-{datetime.date.today():%Y%m%d}-{commit}{'-dirty' if dirty else ''}.bin"

out = repo / 'dist/openhouse-basestation-firmware'
shutil.rmtree(out, ignore_errors=True)
(out / 'firmware').mkdir(parents=True)
shutil.copy2(image, out / 'firmware' / name)
for f in ['install-firmware.sh', 'Install-Firmware.ps1', 'Install-Firmware.bat']:
    shutil.copy2(fw / 'installer' / f, out / f)
shutil.copy2(repo / 'LICENSE', out / 'LICENSE.txt')
(out / 'install-firmware.sh').chmod(0o755)

archive = repo / 'dist/openhouse-basestation-firmware.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(out.rglob('*')):
        if path.is_dir():
            continue
        info = zipfile.ZipInfo.from_file(path, Path(out.name) / path.relative_to(out))
        mode = 0o755 if path.name.endswith('.sh') else 0o644
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, path.read_bytes())
print(f'Packaged {archive} ({name}, {len(data)} bytes)')
