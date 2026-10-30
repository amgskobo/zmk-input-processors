# SPDX-License-Identifier: MIT
"""Keep operation instrumentation separate from canonical arithmetic coverage."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScalerFastPathTests(unittest.TestCase):
    def test_unit_ratios_skip_checked_products(self):
        with tempfile.TemporaryDirectory(prefix="scaler-fastpath-") as folder:
            for optimization in ("-O0", "-O2"):
                binary = Path(folder) / optimization[1:]
                subprocess.run([
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", optimization,
                    "-I" + str(ROOT / "include"), str(ROOT / "tests/cost/scaler_fastpath.c"),
                    "-o", str(binary),
                ], check=True)
                subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
