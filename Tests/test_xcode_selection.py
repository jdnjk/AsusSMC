# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'Scripts' / 'select-xcode.py'
SPEC = importlib.util.spec_from_file_location('asussmc_xcode_selection', SCRIPT)
XCODE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(XCODE)


class XcodeSelectionTests(unittest.TestCase):
    @staticmethod
    def install(root, name):
        tool = root / name / 'Contents/Developer/usr/bin/xcodebuild'
        tool.parent.mkdir(parents=True)
        tool.write_text('fixture, not an executable SDK')
        return tool.parents[2].resolve()

    def test_highest_stable_major_26(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.install(root, 'Xcode_16.4.app')
            self.install(root, 'Xcode_26.1.1.app')
            expected = self.install(root, 'Xcode_26.3.app')
            self.install(root, 'Xcode_26.4_beta.app')
            self.install(root, 'Xcode_27.0.app')
            self.assertEqual(XCODE.select_xcode(root, 26), expected)

    def test_no_fallback_to_xcode_16(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.install(root, 'Xcode_16.4.app')
            with self.assertRaises(FileNotFoundError):
                XCODE.select_xcode(root, 26)

    def test_patch_versions_sort_numerically(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.install(root, 'Xcode_26.2.9.app')
            expected = self.install(root, 'Xcode_26.2.10.app')
            self.assertEqual(XCODE.select_xcode(root, 26), expected)

    def test_incomplete_bundle_is_not_selected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            expected = self.install(root, 'Xcode_26.2.app')
            (root / 'Xcode_26.9.app').mkdir()
            self.assertEqual(XCODE.select_xcode(root, 26), expected)


if __name__ == '__main__':
    unittest.main()
