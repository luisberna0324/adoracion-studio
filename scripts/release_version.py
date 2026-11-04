#!/usr/bin/env python3
"""Read or verify the canonical release version, optionally at a specific Git ref."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def read_version(ref=None):
    if ref:
        text = subprocess.check_output(
            ['git', '-C', str(ROOT), 'show', f'{ref}:src/AppVersion.h'], text=True)
    else:
        text = (ROOT / 'src/AppVersion.h').read_text()
    matches = re.findall(r'^#define PLAYWORSHIP_RELEASE_VERSION "(\d+\.\d+\.\d+)"$', text, re.M)
    if len(matches) != 1:
        raise ValueError('Expected one canonical SemVer in src/AppVersion.h')
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ref', help='Read the version from this revision instead of the working tree')
    parser.add_argument('--expect', help='Reject a pipeline/package version that differs from source')
    args = parser.parse_args()
    try:
        version = read_version(args.ref)
        if args.expect and args.expect != version:
            parser.error(f'Pipeline version {args.expect} differs from source {version}; select the matching release_kind or update src/AppVersion.h')
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    print(version)


if __name__ == '__main__':
    main()
