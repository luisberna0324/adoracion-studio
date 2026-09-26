"""Run a filesystem regression test with a fresh, private app-data root."""

import os
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: run_isolated_test.py <test executable> [args...]", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="playworship-creation-") as root:
        environment = os.environ.copy()
        environment["PLAYWORSHIP_CREATION_TEST_ROOT"] = root
        result = subprocess.run(sys.argv[1:], env=environment, check=False)
        if result.returncode == 77:
            print("isolated test unexpectedly skipped", file=sys.stderr)
            return 1
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
