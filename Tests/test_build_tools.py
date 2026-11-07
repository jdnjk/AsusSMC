# SPDX-License-Identifier: MIT
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('asussmc_build_deps', ROOT / 'Scripts' / 'prepare-deps.py')
DEPS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DEPS)


class SDKPreparationTests(unittest.TestCase):
    def test_lock_pins_official_versions_and_sha256(self):
        lock = json.loads((ROOT / 'Scripts' / 'dependencies.lock.json').read_text())
        self.assertEqual(lock['schemaVersion'], 1)
        self.assertEqual(lock['MacKernelSDK']['repository'], 'https://github.com/acidanthera/MacKernelSDK.git')
        self.assertRegex(lock['MacKernelSDK']['revision'], r'^[0-9a-f]{40}$')
        for name in ('Lilu', 'VirtualSMC'):
            self.assertEqual(lock[name]['repository'], f'acidanthera/{name}')
            self.assertEqual(lock[name]['asset'], f'{name}-{lock[name]["version"]}-DEBUG.zip')
            self.assertRegex(lock[name]['sha256'], r'^[0-9a-f]{64}$')

    def test_extract_regular_sdk_header(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'sdk.zip'
            with zipfile.ZipFile(archive, 'w') as handle:
                handle.writestr('Lilu.kext/Contents/Resources/Headers/kern_util.hpp', '// test header')
            DEPS.checked_extract(archive, root / 'new')
            self.assertTrue((root / 'new/Lilu.kext/Contents/Resources/Headers/kern_util.hpp').is_file())

    def test_reject_traversal_and_windows_paths(self):
        for member in ('../outside', '/outside', 'C:/outside', r'..\outside'):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                archive = root / 'sdk.zip'
                with zipfile.ZipFile(archive, 'w') as handle:
                    handle.writestr(member, 'not a header')
                with self.assertRaises(ValueError):
                    DEPS.checked_extract(archive, root / 'new')
                self.assertFalse((root / 'outside').exists())

    def test_reject_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'sdk.zip'
            with zipfile.ZipFile(archive, 'w') as handle:
                entry = zipfile.ZipInfo('header-link')
                entry.create_system = 3
                entry.external_attr = 0o120777 << 16
                handle.writestr(entry, '/outside')
            with self.assertRaises(ValueError):
                DEPS.checked_extract(archive, root / 'new')

    def test_refuse_existing_extraction_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            archive = root / 'sdk.zip'
            with zipfile.ZipFile(archive, 'w') as handle:
                handle.writestr('header', 'data')
            (root / 'existing').mkdir()
            (root / 'existing/keep.txt').write_text('keep')
            with self.assertRaises(FileExistsError):
                DEPS.checked_extract(archive, root / 'existing')
            self.assertEqual((root / 'existing/keep.txt').read_text(), 'keep')

    def test_existing_dependency_is_never_deleted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'Lilu.kext').mkdir()
            (root / 'Lilu.kext/keep.txt').write_text('keep')
            source = root / 'pinned'
            source.mkdir()
            (source / 'header').write_text('header')
            with self.assertRaises(FileExistsError):
                DEPS.link_sdk(root, 'Lilu.kext', source, 'header')
            self.assertEqual((root / 'Lilu.kext/keep.txt').read_text(), 'keep')

    @unittest.skipIf(os.name == 'nt', 'Directory symlinks may require Windows administrator privileges')
    def test_pinned_sdk_link_is_reusable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / 'pinned'
            source.mkdir()
            (source / 'header').write_text('header')
            DEPS.link_sdk(root, 'Lilu.kext', source, 'header')
            DEPS.link_sdk(root, 'Lilu.kext', source, 'header')
            self.assertEqual((root / 'Lilu.kext').resolve(), source.resolve())


if __name__ == '__main__':
    unittest.main()
