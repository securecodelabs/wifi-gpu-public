#!/usr/bin/env python3
"""Opt-in stock memory profile for the tested RX 9060 XT (AMD 1002:7590).

Run on the Linux host, while GPU jobs are idle. Does not change clocks through
overdrive, voltage, or the power cap. Restore returns to auto/BOOTUP_DEFAULT.
"""
import argparse
import json
from pathlib import Path
import re


def read(device, name):
    return (device / name).read_text(encoding='ascii').strip()


def validate(device):
    if (read(device, 'vendor'), read(device, 'device')) != ('0x1002', '0x7590'):
        raise ValueError('This profile is validated only for AMD PCI device 1002:7590')
    if not re.search(r'^\s*1:\s+456[Mm][Hh][Zz]\s*\*?\s*$',
                     read(device, 'pp_dpm_mclk'), re.M):
        raise ValueError('Expected stock memory DPM level 1 at 456 MHz')


def state(device):
    return {'performance_level': read(device, 'power_dpm_force_performance_level'),
            'memory_levels': read(device, 'pp_dpm_mclk'),
            'power_profile': read(device, 'pp_power_profile_mode')}


def restore(device):
    # Always attempt auto, even if resetting the profile fails.
    try:
        (device / 'power_dpm_force_performance_level').write_text('manual', encoding='ascii')
        (device / 'pp_power_profile_mode').write_text('0', encoding='ascii')
    finally:
        (device / 'power_dpm_force_performance_level').write_text('auto', encoding='ascii')


def apply(device):
    validate(device)
    before = state(device)
    if before['performance_level'] != 'auto' or not re.search(
            r'^\s*0\s+BOOTUP_DEFAULT\s*\*', before['power_profile'], re.M):
        raise ValueError('Start from auto / BOOTUP_DEFAULT; refusing to replace another policy')
    try:
        (device / 'power_dpm_force_performance_level').write_text('manual', encoding='ascii')
        (device / 'pp_dpm_mclk').write_text('1', encoding='ascii')
        after = state(device)
        if after['performance_level'] != 'manual' or not re.search(
                r'^\s*1:\s+456[Mm][Hh][Zz]\s+\*\s*$', after['memory_levels'], re.M):
            raise RuntimeError('Driver did not confirm the requested memory profile')
    except BaseException:
        restore(device)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('apply', 'restore', 'status'))
    parser.add_argument('--device', type=Path, required=True,
                        help='Host sysfs PCI device, e.g. /sys/bus/pci/devices/0000:01:00.0')
    args = parser.parse_args()
    device = args.device.resolve(strict=True)
    if not device.is_relative_to(Path('/sys/devices')):
        parser.error('--device must resolve below /sys/devices')
    validate(device)
    if args.action == 'apply':
        apply(device)
    elif args.action == 'restore':
        restore(device)
    print(json.dumps(state(device), indent=2))


if __name__ == '__main__':
    main()
