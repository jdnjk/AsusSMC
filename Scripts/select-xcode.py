#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Select an installed stable Xcode major without silently falling back to an older SDK."""
import argparse
from pathlib import Path
import re
import sys


def select_xcode(applications, major):
    candidates = []
    for bundle in Path(applications).glob('Xcode_*.app'):
        match = re.fullmatch(r'Xcode_(\d+)\.(\d+)(?:\.(\d+))?\.app', bundle.name)
        if not match or int(match[1]) != major:
            continue
        version = tuple(int(part or 0) for part in match.groups())
        developer = bundle / 'Contents' / 'Developer'
        if (developer / 'usr' / 'bin' / 'xcodebuild').is_file():
            candidates.append((version, developer))
    if not candidates:
        raise FileNotFoundError(f'No stable Xcode {major}.x installation in {applications}; refusing an older SDK fallback')
    return max(candidates, key=lambda item: (item[0], str(item[1])))[1].resolve()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--applications', type=Path, default=Path('/Applications'))
    parser.add_argument('--major', type=int, default=26)
    args = parser.parse_args()
    try:
        print(select_xcode(args.applications, args.major))
    except (FileNotFoundError, OSError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(2)
