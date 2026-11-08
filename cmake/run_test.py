# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Run a CTest binary with a fresh TEST_TMPDIR for each invocation."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parent = Path(sys.argv[1])
    parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as temporary:
        environment = os.environ.copy()
        environment["TEST_TMPDIR"] = temporary
        return subprocess.run(sys.argv[2:], env=environment, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
