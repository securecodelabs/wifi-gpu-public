"""Exercise device guards and rollback using an in-memory sysfs model."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('profile', Path(__file__).resolve().parents[1] / 'scripts/amd-memory-profile.py')
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.values = {'vendor': '0x1002', 'device': '0x7590',
                       'power_dpm_force_performance_level': 'auto',
                       'pp_dpm_mclk': '0: 96Mhz *\n1: 456Mhz',
                       'pp_power_profile_mode': '0 BOOTUP_DEFAULT*:'}
        self.writes = []
        self.fail_memory = False
        self.reject_memory = False
        self.device = Path('/fake/gpu')

    def write(self, path, value, **kwargs):
        self.writes.append((path.name, value))
        if path.name == 'pp_dpm_mclk':
            if self.fail_memory:
                raise OSError('simulated driver failure')
            if not self.reject_memory:
                self.values[path.name] = '0: 96Mhz\n1: 456Mhz *'
        else:
            self.values[path.name] = value

    def run_apply(self):
        with patch.object(profile, 'read', side_effect=lambda device, name: self.values[name]), \
             patch.object(Path, 'write_text', autospec=True, side_effect=self.write):
            profile.apply(self.device)

    def test_apply_sets_only_stock_policy(self):
        self.run_apply()
        self.assertEqual(self.writes, [('power_dpm_force_performance_level', 'manual'), ('pp_dpm_mclk', '1')])

    def test_rejects_wrong_device_before_writes(self):
        self.values['device'] = '0x9999'
        with self.assertRaises(ValueError): self.run_apply()
        self.assertEqual(self.writes, [])

    def test_rejects_changed_memory_table(self):
        self.values['pp_dpm_mclk'] = '1: 1000Mhz'
        with self.assertRaises(ValueError): self.run_apply()
        self.assertEqual(self.writes, [])

    def test_preserves_existing_custom_policy(self):
        self.values['power_dpm_force_performance_level'] = 'manual'
        with self.assertRaises(ValueError): self.run_apply()
        self.assertEqual(self.writes, [])

    def test_restores_after_driver_write_failure(self):
        self.fail_memory = True
        with self.assertRaises(OSError): self.run_apply()
        self.assertEqual(self.values['power_dpm_force_performance_level'], 'auto')
        self.assertIn(('pp_power_profile_mode', '0'), self.writes)

    def test_restores_if_driver_does_not_apply(self):
        self.reject_memory = True
        with self.assertRaises(RuntimeError): self.run_apply()
        self.assertEqual(self.values['power_dpm_force_performance_level'], 'auto')


if __name__ == '__main__':
    unittest.main()
