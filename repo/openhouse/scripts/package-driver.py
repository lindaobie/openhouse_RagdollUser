#!/usr/bin/env python3
"""Bundle non-platform ELF dependencies for hosts running inside Steam Runtime.

Run on our own built driver, not arbitrary untrusted ELF files (ldd is used).
Never bundle glibc, libstdc++, libgcc, or the dynamic loader: these belong to host.
"""
import pathlib
import hashlib
import json
import shutil
import subprocess
import sys

# GPL libraries (SuiteSparse's SPQR/CHOLMOD, via a distro Ceres) must never ship with the
# MIT driver: build with scripts/build-linux.sh (vcpkg Ceres without SuiteSparse).
FORBIDDEN = ('libspqr', 'libcholmod')

PLATFORM = {'libc.so.6', 'libm.so.6', 'libdl.so.2', 'libpthread.so.0',
            'librt.so.1', 'libstdc++.so.6', 'libgcc_s.so.1', 'libresolv.so.2'}

def package(source, destination):
    source, destination = pathlib.Path(source).resolve(), pathlib.Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    deps = destination / 'lh-libs'
    deps.mkdir(exist_ok=True)
    driver = destination / 'driver_lighthouse.so'
    listing = subprocess.check_output(['ldd', str(source)], text=True)
    if 'not found' in listing:
        raise RuntimeError('Unresolved build dependencies:\n' + listing)
    manifest = []
    licenses = destination / 'lh-licenses'
    licenses.mkdir(exist_ok=True)
    for line in listing.splitlines():
        words = line.split()
        if len(words) < 3 or words[1] != '=>' or not words[2].startswith('/'):
            continue
        name, path = words[0], words[2]
        if name in PLATFORM:
            continue
        if name.startswith(FORBIDDEN):
            raise RuntimeError(f'{name} is GPL and must not be bundled; build with scripts/build-linux.sh')
        target = deps / name
        shutil.copy2(path, target)
        manifest.append({'soname': name, 'source': path,
                         'sha256_original': hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()})
        # Preserve installed distro license notices when available (local bundle).
        if shutil.which('pacman'):
            owner = subprocess.run(['pacman', '-Qqo', path], text=True, capture_output=True)
            if owner.returncode == 0:
                for package_name in owner.stdout.splitlines():
                    notice = pathlib.Path('/usr/share/licenses') / package_name
                    if notice.is_dir():
                        shutil.copytree(notice, licenses / package_name, dirs_exist_ok=True)
        # Each dependency can find its own transitive dependencies too.
        subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
    if source != driver:
        shutil.copy2(source, driver)
    subprocess.run(['patchelf', '--force-rpath', '--set-rpath', '$ORIGIN/lh-libs', str(driver)], check=True)
    subprocess.run(['strip', '--strip-unneeded', str(driver)], check=True)
    (destination / 'lh-dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packaged {driver} and {len(list(deps.iterdir()))} private libraries')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('usage: package-driver.py BUILT_DRIVER DESTINATION_DIRECTORY')
    package(*sys.argv[1:])
