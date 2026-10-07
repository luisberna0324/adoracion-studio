#!/usr/bin/env python3
"""Add the Apple capability to JUCE entitlements without dropping sandbox keys."""
import argparse
import plistlib
from pathlib import Path


def merge(source: Path, destination: Path) -> None:
    with source.open("rb") as stream:
        entitlements = plistlib.load(stream)
    if not isinstance(entitlements, dict):
        raise ValueError("The JUCE entitlements must be a dictionary")
    key = "com.apple.developer.applesignin"
    if key in entitlements and entitlements[key] != ["Default"]:
        raise ValueError("Unexpected existing Apple sign-in entitlement")
    entitlements[key] = ["Default"]
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(plistlib.dumps(entitlements, sort_keys=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    merge(args.source, args.destination)
