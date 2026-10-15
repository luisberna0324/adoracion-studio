"""Render real JUCE blank-song components using a built Ninja test target.

Build MTControlsSongWizardTests first. Reuses its actual JUCE/Bungee objects,
compiles the preview translation unit with identical flags, and renders with
private app-data under the existing isolated runner. No fabricated UI or audio.
"""
import argparse
from pathlib import Path
import shlex
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_dir", type=Path)
parser.add_argument("output_dir", type=Path)
options = parser.parse_args()
root = Path(__file__).resolve().parents[1]
build = options.build_dir.resolve()
output = options.output_dir.resolve()
preview_dir = build / "blank-ui-preview"
preview_dir.mkdir(parents=True, exist_ok=True)
commands = subprocess.check_output(
    ["ninja", "-C", str(build), "-t", "commands", "MTControlsSongWizardTests"],
    text=True,
).splitlines()
compile_line = next(line for line in commands
                    if " -c " in line and "/tests/song_import_wizard_tests.cpp" in line)
arguments = shlex.split(compile_line)
for index, argument in enumerate(arguments):
    if argument.endswith("/tests/song_import_wizard_tests.cpp"):
        arguments[index] = str(root / "tests/blank_song_ui_preview.cpp")
    elif "song_import_wizard_tests.cpp.o" in argument:
        arguments[index] = str(preview_dir / "preview.o") + (".d" if argument.endswith(".d") else "")
subprocess.run(arguments, cwd=build, check=True)
link = shlex.split(commands[-1])
if link[:2] == [":", "&&"]:
    link = link[2:-2]
executable = preview_dir / "blank-song-preview"
for index, argument in enumerate(link):
    if argument.startswith("-Wl,--dependency-file="):
        link[index] = "-Wl,--dependency-file=" + str(preview_dir / "link.d")
    elif argument.endswith("tests/song_import_wizard_tests.cpp.o"):
        link[index] = str(preview_dir / "preview.o")
    elif index > 0 and link[index - 1] == "-o":
        link[index] = str(executable)
subprocess.run(link, cwd=build, check=True)
subprocess.run([sys.executable, str(root / "tests/run_isolated_test.py"),
                str(executable), str(output)], cwd=root, check=True)
