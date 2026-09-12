#!/usr/bin/env python3
"""
analyze_bloat.py — cuantifica archivos huérfanos en ~/Library/MTControls/.

Cruza lo que está en disco contra lo que songs.json referencia. Imprime
para cada directorio (decoded/, cloud-content/objects/, cloud-artwork/,
cloud-sync-staging/, projects/) cuántos archivos son referenciados y
cuántos son huérfanos (potencial cruft recuperable).

Uso:
    python3 analyze_bloat.py
    python3 analyze_bloat.py --root /otro/path    # testing
"""

import argparse
import json
import os
import sys
import unicodedata


def normalize(s: str) -> str:
    """Strip diacritics + lower, replace _ with space. Used to fuzzy-match
    StemCache's sanitized titles (e.g. 'Glorioso Dia' vs 'Glorioso Día')."""
    out = unicodedata.normalize("NFD", s)
    out = "".join(c for c in out if unicodedata.category(c) != "Mn")
    return out.lower().replace("_", " ")


def dir_size(path: str) -> int:
    total = 0
    for r, _, files in os.walk(path):
        for f in files:
            try:
                total += os.path.getsize(os.path.join(r, f))
            except OSError:
                pass
    return total


def referenced_paths(songs: list, contains: str) -> set:
    """Return set of basenames referenced from songs.json whose path contains
    one of the given substrings."""
    ref = set()
    for s in songs:
        for stem in s.get("stems", []):
            for k in ("storedPath", "sourcePath"):
                p = stem.get(k, "")
                if p and contains in p:
                    ref.add(os.path.basename(p))
        for k in ("artworkPath",):
            p = s.get(k, "")
            if p and contains in p:
                ref.add(os.path.basename(p))
    return ref


def analyze_decoded(root: str, songs: list) -> None:
    decoded = os.path.join(root, "decoded")
    if not os.path.isdir(decoded):
        print("decoded/  (missing)")
        return
    titles = [s.get("title", "").strip() for s in songs]
    print("decoded/")
    for entry in sorted(os.listdir(decoded)):
        path = os.path.join(decoded, entry)
        if not os.path.isdir(path):
            continue
        size = dir_size(path)
        norm_entry = normalize(entry)
        # StemCache uses sanitizeCacheKey which replaces problematic chars
        # with `_`. Try exact (normalized) match first, then fuzzy.
        is_ref = any(
            normalize(t) == norm_entry
            or normalize(t).replace(" ", "_") == entry
            for t in titles
        )
        marker = "" if is_ref else "  <-- ORPHAN"
        print(f"  {entry:50s}  {size//1024//1024:>5} MB{marker}")


def analyze_objects(root: str, songs: list) -> None:
    p = os.path.join(root, "cloud-content", "objects")
    if not os.path.isdir(p):
        print("cloud-content/objects/  (missing)")
        return
    ref = referenced_paths(songs, "cloud-content/objects/")
    files = set(os.listdir(p))
    orphan = sorted(files - ref)
    total = sum(
        os.path.getsize(os.path.join(p, f))
        for f in orphan
        if os.path.isfile(os.path.join(p, f))
    )
    print(f"cloud-content/objects/  {len(ref)} referenced, "
          f"{len(files)} on disk, {len(orphan)} orphan "
          f"({total//1024//1024} MB)")
    for f in orphan:
        full = os.path.join(p, f)
        if os.path.isfile(full):
            print(f"  {f:80s}  {os.path.getsize(full)//1024//1024:>4} MB")
        else:
            print(f"  {f}  (dir)")


def analyze_artwork(root: str, songs: list) -> None:
    p = os.path.join(root, "cloud-artwork")
    if not os.path.isdir(p):
        print("cloud-artwork/  (missing)")
        return
    ref = referenced_paths(songs, "cloud-artwork/")
    files = set(os.listdir(p))
    orphan = sorted(files - ref)
    print(f"cloud-artwork/  {len(ref)} referenced, "
          f"{len(files)} on disk, {len(orphan)} orphan")
    for f in orphan:
        print(f"  {f}")


def analyze_sync_staging(root: str, songs: list) -> None:
    p = os.path.join(root, "cloud-sync-staging")
    if not os.path.isdir(p):
        print("cloud-sync-staging/  (missing)")
        return
    ref_ids = set()
    for s in songs:
        for k in ("id", "projectId"):
            v = s.get(k, "")
            if v:
                ref_ids.add(v)
    print(f"cloud-sync-staging/  referenced={len(ref_ids)}")
    for entry in sorted(os.listdir(p)):
        full = os.path.join(p, entry)
        if not os.path.isdir(full):
            continue
        size = dir_size(full)
        marker = "" if entry in ref_ids else "  <-- ORPHAN"
        print(f"  {entry:40s}  {size//1024:>6} KB{marker}")


def analyze_projects(root: str, songs: list) -> None:
    p = os.path.join(root, "projects")
    if not os.path.isdir(p):
        print("projects/  (missing)")
        return
    files = sorted(os.listdir(p))
    print(f"projects/  {len(files)} files")
    for f in files:
        full = os.path.join(p, f)
        if os.path.isfile(full):
            print(f"  {f:60s}  {os.path.getsize(full)//1024//1024:>4} MB")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=os.path.expanduser("~/Library/MTControls"))
    args = parser.parse_args()

    songs_json = os.path.join(args.root, "songs.json")
    if not os.path.isfile(songs_json):
        print(f"ERROR: {songs_json} not found", file=sys.stderr)
        sys.exit(1)

    with open(songs_json) as f:
        data = json.load(f)
    songs = data.get("songs", [])
    print(f"Library: {len(songs)} songs in {songs_json}\n")

    analyze_decoded(args.root, songs)
    print()
    analyze_objects(args.root, songs)
    print()
    analyze_artwork(args.root, songs)
    print()
    analyze_sync_staging(args.root, songs)
    print()
    analyze_projects(args.root, songs)


if __name__ == "__main__":
    main()
