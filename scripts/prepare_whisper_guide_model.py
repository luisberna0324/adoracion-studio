#!/usr/bin/env python3
"""Prepare the pinned local guide-ASR model; never reads or uploads audio.

The SHA-256 and length below come from the model's upstream Git LFS pointer:
https://huggingface.co/ggerganov/whisper.cpp/raw/f281eb45af861ab5e5297d23694b7d46e090c02c/ggml-base-q5_1.bin
whisper.cpp v1.9.5 documents this model repository in models/README.md.
Python 3.10+, standard library only. Run --verify-only for an offline check.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile
from urllib.request import Request, urlopen


MODEL_NAME = "ggml-base-q5_1.bin"
MODEL_REVISION = "f281eb45af861ab5e5297d23694b7d46e090c02c"
MODEL_SHA256 = "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898"
MODEL_BYTES = 59_707_625
MODEL_URL = (
    "https://huggingface.co/ggerganov/whisper.cpp/resolve/"
    f"{MODEL_REVISION}/{MODEL_NAME}"
)
CHUNK_BYTES = 1024 * 1024


def default_destination() -> Path:
    """Match JUCE File::userApplicationDataDirectory on desktop platforms."""
    if sys.platform == "darwin":
        app_data = Path.home() / "Library"
    elif sys.platform == "win32":
        roaming = os.environ.get("APPDATA")
        if not roaming:
            raise RuntimeError("APPDATA is unavailable; pass --destination explicitly.")
        app_data = Path(roaming)
    elif sys.platform.startswith("linux"):
        app_data = Path.home() / ".config"
        # JUCE resolves this through user-dirs.dirs, not the process environment.
        config = app_data / "user-dirs.dirs"
        if config.is_file():
            for line in config.read_text(encoding="utf-8").splitlines():
                if line.strip().startswith("XDG_CONFIG_HOME") and "=" in line:
                    folder = line.split("=", 1)[1].strip().strip("\"'")
                    candidate = Path(folder.replace("$HOME", str(Path.home())))
                    if candidate.is_absolute() and candidate.is_dir():
                        app_data = candidate
                        break
    else:
        raise RuntimeError("Pass --destination explicitly on this platform.")
    return app_data / "MTControls" / "models" / MODEL_NAME


def verify_model(path: Path) -> None:
    if not path.is_file():
        raise RuntimeError(f"Model file not found: {path}")
    if path.stat().st_size != MODEL_BYTES:
        raise RuntimeError(f"Model has an unexpected size: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(CHUNK_BYTES):
            digest.update(chunk)
    if digest.hexdigest() != MODEL_SHA256:
        raise RuntimeError(f"Model SHA-256 does not match the pinned model: {path}")


def prepare_model(destination: Path, verify_only: bool) -> None:
    if destination.exists():
        try:
            verify_model(destination)
        except RuntimeError as error:
            raise RuntimeError(
                f"{error}\nMove that file or choose another --destination before retrying."
            ) from error
        print(f"Verified local model (no network): {destination}")
        return
    if verify_only:
        verify_model(destination)
        return

    print(f"Downloading only {MODEL_NAME} ({MODEL_BYTES:,} bytes, about 57 MiB).")
    print(f"Destination: {destination}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{MODEL_NAME}.", suffix=".part",
            dir=destination.parent, delete=False,
        ) as target:
            temporary = Path(target.name)
            digest = hashlib.sha256()
            received = 0
            request = Request(MODEL_URL, headers={"User-Agent": "PlayWorship-model-preparation/1"})
            with urlopen(request, timeout=60) as response:
                while chunk := response.read(CHUNK_BYTES):
                    received += len(chunk)
                    if received > MODEL_BYTES:
                        raise RuntimeError("Download exceeds the pinned model size.")
                    target.write(chunk)
                    digest.update(chunk)
            if received != MODEL_BYTES or digest.hexdigest() != MODEL_SHA256:
                raise RuntimeError("Downloaded model failed the pinned size/SHA-256 check.")
            target.flush()
            os.fsync(target.fileno())

        # Another invocation may have finished while this one was downloading.
        # Preserve an already present file and require it to be the pinned model.
        if destination.exists():
            verify_model(destination)
        else:
            os.replace(temporary, destination)
            temporary = None
        print(f"Verified and ready for offline transcription: {destination}")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--destination", type=Path,
        help="Full output file path; default: JUCE AppData/MTControls/models/ggml-base-q5_1.bin.",
    )
    parser.add_argument(
        "--verify-only", action="store_true",
        help="Check the local file's pinned size and SHA-256 without network access.",
    )
    args = parser.parse_args()
    try:
        destination = (args.destination or default_destination()).expanduser().absolute()
        prepare_model(destination, args.verify_only)
    except KeyboardInterrupt:
        print("Cancelled; incomplete model removed.", file=sys.stderr)
        return 130
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Model preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
