# SPDX-License-Identifier: MIT
"""The public scaler must compile without software wide-integer arithmetic."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class ScalerWidth(unittest.TestCase):
    def test_32bit_object_has_no_wide_arithmetic_helpers(self):
        include = Path(os.environ.get('ZMK_SCALER_TEST_INCLUDE',
                                      str(Path(__file__).resolve().parents[1] / 'include')))
        with tempfile.TemporaryDirectory(prefix='scaler-width-') as folder:
            root = Path(folder)
            source, obj = root / 'probe.c', root / 'probe.o'
            source.write_text('''#include <zmk-input-processors/runtime_scaler_math.h>
int32_t probe(int32_t value, uint32_t mul, uint32_t div, int16_t *rest) {
    return runtime_scaler_scale(value, mul, div, rest);
}
''')
            subprocess.run(['cc', '-m32', '-O2', '-std=c11', '-I', str(include),
                            '-c', str(source), '-o', str(obj)], check=True,
                           capture_output=True, text=True)
            symbols = subprocess.check_output(['nm', '--undefined-only', str(obj)], text=True)
            for helper in ('__divdi3', '__moddi3', '__divmoddi4', '__udivdi3',
                           '__umoddi3', '__udivmoddi4', '__muldi3'):
                self.assertNotIn(helper, symbols, symbols)


if __name__ == '__main__':
    unittest.main()
