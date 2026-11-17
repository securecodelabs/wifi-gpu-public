"""Runtime-selection and truthful benchmark-report checks; no GPU is required."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    'rocm_opencl', Path(__file__).resolve().parents[1]/'scripts/rocm-opencl.py')
rocm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rocm)
AMD = 'Device 0: gfx1200 (Advanced Micro Devices, Inc.)\n'
FINAL = ('status=candidate_limit next=65536 candidates=65536 rejected=0 '
         'recovered=0/1 derivations=65536 elapsed_s=2 derivations_per_second=32768\n')


class RocmTests(unittest.TestCase):
    def test_isolated_runtime_keeps_parent_environment_unchanged(self):
        inherited = {'OCL_ICD_VENDORS': '/system/vendors', 'OCL_ICD_FILENAMES': 'another.so',
                     'OPENCL_LAYERS': 'trace.so', 'PATH': 'retained'}
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)/'vendors'
            env = rocm.runtime_environment(folder, 'libamdocl64.so', inherited)
            self.assertEqual(env['OCL_ICD_VENDORS'], str(folder))
            self.assertNotIn('OCL_ICD_FILENAMES', env)
            self.assertEqual(env['OPENCL_LAYERS'], '')
            self.assertEqual(env['PATH'], 'retained')
            self.assertEqual(inherited['OCL_ICD_FILENAMES'], 'another.so')
            self.assertEqual((folder/'amdocl64.icd').read_text(), 'libamdocl64.so\n')

    def test_icd_requires_one_amd_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)/'amd.icd'
            for text in ('', 'libamdocl64.so\nother.so\n', 'libMesaOpenCL.so\n',
                         '../lib/libamdocl64.so\n'):
                path.write_text(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    rocm.amd_icd(path)
            path.write_text('libamdocl64.so.1\n')
            self.assertEqual(rocm.amd_icd(path), 'libamdocl64.so.1')
            library = Path(temporary)/'libamdocl64.so'
            path.write_text(str(library)+'\n')
            with self.assertRaises(ValueError):
                rocm.amd_icd(path)
            library.touch()
            self.assertEqual(rocm.amd_icd(path), str(library))

    def test_device_selection_cannot_silently_fall_back(self):
        self.assertIn('gfx1200', rocm.selected_amd(AMD, 0))
        self.assertIn('AMD', rocm.selected_amd('Device 2: Radeon (AMD)\n', 2))
        for text, index in ((AMD, 1), ('Device 0: GTX (NVIDIA)\n', 0),
                            ('Device 0: llvmpipe (AMD)\n', 0), ('', 0)):
            with self.subTest(text=text, index=index), self.assertRaises(ValueError):
                rocm.selected_amd(text, index)

    def test_recovery_requires_all_candidates_and_consistent_rate(self):
        self.assertEqual(rocm.recovery_sample(FINAL, 65536, 3)['search_candidates_per_second'], 32768)
        for text in (FINAL.replace('next=65536', 'next=65535'),
                     FINAL.replace('derivations=65536', 'derivations=131072'),
                     FINAL.replace('rejected=0', 'rejected=1'),
                     FINAL.replace('32768', '99999'),
                     FINAL.replace('candidate_limit', 'runtime_limit'),
                     'FOUND password=test\n'+FINAL):
            with self.subTest(text=text), self.assertRaises(ValueError):
                rocm.recovery_sample(text, 65536, 3)

    def test_timeout_retains_diagnostics_and_is_not_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            run = rocm.Run(Path(temporary), dict(os.environ))
            failure = subprocess.TimeoutExpired(['test'], 1, output=b'partial output', stderr=b'error')
            with patch.object(rocm.subprocess, 'run', side_effect=failure), self.assertRaises(RuntimeError):
                run.command('timed-out', ['test'], timeout=1)
            self.assertEqual(run.report['commands'][0]['error'], 'timeout')
            self.assertIn('partial outputerror', (Path(temporary)/'timed-out.txt').read_text())
            self.assertFalse(run.report['hardware_validated'])

    def test_root_is_rejected_before_runtime_access(self):
        with patch.object(rocm.platform, 'system', return_value='Linux'), \
                patch.object(rocm.os, 'geteuid', return_value=0, create=True), \
                patch.object(rocm, 'amd_icd') as read_icd, self.assertRaises(SystemExit) as error:
            rocm.main(['--exe', 'engine', '--icd', 'amd.icd'])
        self.assertEqual(error.exception.code, 2)
        read_icd.assert_not_called()

    def test_invalid_limits_are_rejected_before_runtime_access(self):
        for options in (['--device', '-1'], ['--batch', '262145'], ['--group', '0'],
                        ['--chunk', '1025'], ['--repeats', '0'], ['--limit', '1024']):
            with self.subTest(options=options), \
                    patch.object(rocm.platform, 'system', return_value='Linux'), \
                    patch.object(rocm.os, 'geteuid', return_value=1000, create=True), \
                    patch.object(rocm, 'amd_icd') as read_icd, self.assertRaises(SystemExit) as error:
                rocm.main(['--exe', 'engine', '--icd', 'amd.icd', *options])
            self.assertEqual(error.exception.code, 2)
            read_icd.assert_not_called()

    def invoke(self, check_only=False, fail=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            exe = root/'wifi-gpu'
            exe.touch()
            (root/'kernels').mkdir()
            (root/'kernels/pbkdf2.cl').touch()
            icd = root/'amd.icd'
            icd.write_text('libamdocl64.so\n')
            calls = []

            def command(run, name, arguments, **kwargs):
                calls.append(name)
                if name == fail:
                    raise RuntimeError('simulated failure')
                if name == 'devices':
                    return AMD, 1
                if name == 'known-positive':
                    Path(arguments[-1]).write_text(json.dumps({
                        'status': 'recovered', 'recovered': 1,
                        'matches': [{'verified': True, 'password_hex': b'000000FFFF'.hex()}],
                        'hardware': {'backend': 'opencl', 'compute_kind': 'gpu'}}))
                if name == 'warmup' or name.startswith('sample-'):
                    return FINAL, 3
                return '', 1

            args = ['--exe', str(exe), '--icd', str(icd), '--limit', '65536',
                    '--repeats', '2', '--output-dir', str(root/'runs')]
            if check_only:
                args.append('--check-only')
            with patch.object(rocm.platform, 'system', return_value='Linux'), \
                    patch.object(rocm.os, 'geteuid', return_value=1000, create=True), \
                    patch.object(rocm.Run, 'command', autospec=True, side_effect=command):
                code = rocm.main(args)
            report = json.loads(next((root/'runs').glob('*/report.json')).read_text())
            return code, report, calls

    def test_preflight_never_claims_hardware_or_speed(self):
        code, report, calls = self.invoke(check_only=True)
        self.assertEqual(code, 0)
        self.assertEqual(report['status'], 'preflight_passed')
        self.assertFalse(report['hardware_validated'])
        self.assertNotIn('median_candidates_per_second', report)
        self.assertNotIn('selftest', calls)

    def test_full_report_requires_correctness_and_measured_samples(self):
        code, report, calls = self.invoke()
        self.assertEqual(code, 0)
        self.assertTrue(report['hardware_validated'])
        self.assertEqual(report['median_candidates_per_second'], 32768)
        self.assertEqual(len(report['samples']), 2)
        self.assertLess(calls.index('known-positive'), calls.index('warmup'))
        self.assertLess(calls.index('selftest'), calls.index('known-positive'))

    def test_failed_correctness_never_reaches_benchmark(self):
        for failure in ('selftest', 'known-positive'):
            with self.subTest(failure=failure):
                code, report, calls = self.invoke(fail=failure)
                self.assertEqual(code, 1)
                self.assertFalse(report['hardware_validated'])
                self.assertEqual(report['status'], 'failed')
                self.assertNotIn('warmup', calls)
                self.assertNotIn('median_candidates_per_second', report)

    def test_incomplete_measurements_have_no_median(self):
        code, report, _ = self.invoke(fail='sample-2')
        self.assertEqual(code, 1)
        self.assertEqual(report['status'], 'failed')
        self.assertNotIn('median_candidates_per_second', report)


if __name__ == '__main__':
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        result = unittest.TextTestRunner(stream=sys.__stdout__, verbosity=2).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(RocmTests))
    raise SystemExit(0 if result.wasSuccessful() else 1)
