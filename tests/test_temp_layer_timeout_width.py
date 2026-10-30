# SPDX-License-Identifier: MIT
"""Exact production timeout header, native32 arithmetic and fault controls."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class TempLayerTimeoutWidth(unittest.TestCase):
    def test_exact_values_width_and_identified_faults(self):
        owner = Path(__file__).resolve().parents[1]
        header = owner / 'drivers/input/runtime_temp_layer_timeout.h'
        original = header.read_text()
        controls = {
            'floor-instead-of-ceil': ('return (ms * hz + 999U) / 1000U;',
                                     'return (ms * hz) / 1000U;'),
            'unchecked-product': ('if (ms <= RUNTIME_TEMP_LAYER_MAX_MS &&\n'
                                  '        hz <= (UINT32_MAX - 999U) / RUNTIME_TEMP_LAYER_MAX_MS)',
                                  'if (1)'),
            'lost-fraction': ('(remainder * (hz % 1000U) + 999U) / 1000U', '0U'),
            'lost-seconds': ('return (k_ticks_t)seconds * hz + fraction;', 'return fraction;'),
            'wide-division': ('if (ms <= RUNTIME_TEMP_LAYER_MAX_MS &&\n'
                              '        hz <= (UINT32_MAX - 999U) / RUNTIME_TEMP_LAYER_MAX_MS) {\n'
                              '        return (ms * hz + 999U) / 1000U;\n    }',
                              'return (k_ticks_t)(((uint64_t)ms * hz + 999U) / 1000U);'),
        }
        with tempfile.TemporaryDirectory(prefix='temp-layer-timeout-') as folder:
            root = Path(folder)
            for ticks in ('64', '32'):
                abi_flags = ['-DRUNTIME_TIMEOUT_32=1'] if ticks == '32' else []
                for name in ('original', *controls):
                    with self.subTest(ticks=ticks, control=name):
                        text = original
                        if name != 'original':
                            before, after = controls[name]
                            self.assertEqual(text.count(before), 1, before)
                            text = text.replace(before, after)
                        include = root / (name + ticks)
                        include.mkdir()
                        (include / header.name).write_text(text)
                        obj, binary = include / 'probe.o', include / 'numeric'
                        common = ['cc', '-m32', '-O2', '-std=c11', *abi_flags,
                                  '-I', str(include), '-I', str(owner / 'include'),
                                  '-I', str(owner / 'tests/timeout-stubs')]
                        unit = str(owner / 'tests/unit/test_temp_layer_timeout.c')
                        subprocess.run([*common, '-DRUNTIME_TIMEOUT_WIDTH_PROBE=1',
                                        '-c', unit, '-o', str(obj)], check=True,
                                       capture_output=True, text=True)
                        symbols = subprocess.check_output(['nm', '--undefined-only', str(obj)], text=True)
                        found = re.findall(r'__(?:u?divdi3|u?moddi3|u?divmoddi4|muldi3)', symbols)
                        if name == 'wide-division':
                            self.assertTrue(found, symbols)
                            print(f'ticks{ticks} compiled wide control rejected: {found}')
                        elif name == 'original':
                            self.assertEqual(found, [], symbols)
                        subprocess.run([*common, unit, '-o', str(binary)], check=True,
                                       capture_output=True, text=True)
                        for repeat in range(3):
                            result = subprocess.run([str(binary)], capture_output=True, text=True)
                            if name in ('original', 'wide-division'):
                                self.assertEqual(result.returncode, 0, result.stderr)
                            else:
                                self.assertEqual(result.returncode, -6, result.stderr)
                                self.assertRegex(result.stderr, r'[^\n]+:[0-9]+: [^\n]+: Assertion .+ failed\.')
                                print(f'ticks{ticks} {name} repeat{repeat + 1}: {result.stderr.strip()}')


if __name__ == '__main__':
    unittest.main()
