#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare pinned development SDKs. Never evaluates downloaded shell/Python code.
Run from the repository root with python3 -I Scripts/prepare-deps.py.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def checked_extract(archive, destination):
    """Extract to a NEW empty directory; reject paths, links and excessive expansion."""
    destination = Path(destination)
    destination.mkdir(exist_ok=False)
    with zipfile.ZipFile(archive) as handle:
        entries = handle.infolist()
        if len(entries) > 50000 or sum(entry.file_size for entry in entries) > 1024 ** 3:
            raise ValueError('SDK archive exceeds extraction limits')
        for entry in entries:
            path = PurePosixPath(entry.filename.replace('\\', '/'))
            kind = (entry.external_attr >> 16) & 0o170000
            if path.is_absolute() or '..' in path.parts or ':' in entry.filename or kind == 0o120000:
                raise ValueError(f'Unsafe SDK archive member: {entry.filename}')
        handle.extractall(destination)


def link_sdk(root, name, source, required):
    target = root / name
    source = source.resolve()
    if target.exists() or target.is_symlink():
        if not target.is_symlink() or target.resolve() != source:
            raise FileExistsError(f'{target} already exists and is not this pinned SDK. Back it up or rename it first; nothing was deleted.')
    else:
        target.symlink_to(source, target_is_directory=True)
    if not (target / required).is_file():
        raise FileNotFoundError(f'SDK header/library missing: {target / required}')


def fetch_bundle(root, cache, name, spec):
    if spec['repository'] != f'acidanthera/{name}' or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', spec['version']):
        raise ValueError('Unexpected SDK origin/version')
    expected_asset = f"{name}-{spec['version']}-DEBUG.zip"
    if spec['asset'] != expected_asset or not re.fullmatch(r'[0-9a-f]{64}', spec['sha256']):
        raise ValueError('Invalid pinned SDK asset/hash')
    vendor = cache / f"{name}-{spec['sha256']}"
    source = vendor / 'extracted' / spec['bundle']
    stamp = vendor / 'verified.sha256'
    if not (stamp.is_file() and stamp.read_text().strip() == spec['sha256'] and (source / spec['header']).is_file()):
        work = Path(tempfile.mkdtemp(prefix=f'{name}-download-', dir=cache))
        archive = work / spec['asset']
        url = f"https://github.com/{spec['repository']}/releases/download/{spec['version']}/{spec['asset']}"
        print(f'Downloading official pinned SDK: {url}', flush=True)
        request = urllib.request.Request(url, headers={'User-Agent': 'AsusSMC-build'})
        with urllib.request.urlopen(request, timeout=180) as response, archive.open('wb') as output:
            shutil.copyfileobj(response, output)
        actual = hashlib.sha256(archive.read_bytes()).hexdigest()
        if actual != spec['sha256']:
            raise ValueError(f'{name} SHA-256 mismatch: expected {spec["sha256"]}, got {actual}; archive preserved in {work}')
        checked_extract(archive, work / 'extracted')
        if not (work / 'extracted' / spec['bundle'] / spec['header']).is_file():
            raise FileNotFoundError(f'{name} release does not contain the expected development header')
        (work / 'verified.sha256').write_text(actual + '\n')
        if vendor.exists():
            raise FileExistsError(f'Incomplete SDK cache already exists: {vendor}; preserve/rename it before retrying')
        work.rename(vendor)
    link_sdk(root, f'{name}.kext', source, spec['header'])


def prepare(root, lock_path):
    spec = json.loads(lock_path.read_text())
    if spec.get('schemaVersion') != 1:
        raise ValueError('Unsupported dependency lock schema')
    cache = root / '.build-deps'
    cache.mkdir(exist_ok=True)
    kernel = spec['MacKernelSDK']
    revision = kernel['revision']
    if kernel['repository'] != 'https://github.com/acidanthera/MacKernelSDK.git' or not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('Unexpected MacKernelSDK origin/revision')
    sdk = cache / f'MacKernelSDK-{revision}'
    if not sdk.exists():
        subprocess.run(['git', 'init', '--quiet', str(sdk)], check=True)
        subprocess.run(['git', '-C', str(sdk), 'remote', 'add', 'origin', kernel['repository']], check=True)
        subprocess.run(['git', '-C', str(sdk), 'fetch', '--depth=1', 'origin', revision], check=True)
        subprocess.run(['git', '-C', str(sdk), 'checkout', '--quiet', '--detach', 'FETCH_HEAD'], check=True)
    actual = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != revision:
        raise ValueError('MacKernelSDK revision mismatch; existing directory preserved')
    link_sdk(root, 'MacKernelSDK', sdk, 'Headers/IOKit/IOUserClient.h')
    if not (sdk / 'Library/x86_64/libkmod.a').is_file():
        raise FileNotFoundError('Pinned MacKernelSDK lacks x86_64/libkmod.a')
    for name in ('Lilu', 'VirtualSMC'):
        fetch_bundle(root, cache, name, spec[name])
    print('Pinned SDK layout and checksums verified.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    prepare(args.root.resolve(), ROOT / 'Scripts' / 'dependencies.lock.json')
