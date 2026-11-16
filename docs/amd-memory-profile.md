# Optional RX 9060 XT host memory profile

On the tested Radeon RX 9060 XT (PCI `1002:7590`), stock memory DPM level 1
(456 MHz) improved full HIP recovery by approximately 0.7% at the unchanged
175 W power cap. See [the repeated benchmark](benchmarks.md).
The engine remains unchanged by this host setting. Other GPUs retain their
normal policies; this helper refuses other PCI device IDs or memory tables.

Run this on the Linux host with Python 3.9 or newer, after GPU jobs are idle.
It works with native engines and container deployments. Containers do not need
root access or permission to write host sysfs. No voltage or overdrive values
are changed. The helper requires the original auto/BOOTUP_DEFAULT policy and
restores it if the driver rejects the requested setting.

From a source checkout, install the helper and optional systemd service:

```sh
sudo install -D -m 0755 scripts/amd-memory-profile.py /usr/local/libexec/wifi-gpu/amd-memory-profile.py
sudo install -m 0644 deploy/wifi-gpu-memory-profile@.service /etc/systemd/system/
sudo systemctl daemon-reload
# Replace the PCI address with the tested GPU's address on your host.
sudo systemctl enable --now wifi-gpu-memory-profile@0000:01:00.0.service
```

Release archives put these two files in `share/wifi-gpu` and this guide in its `docs/` folder;
use those source paths with the same installation destinations. Installation
does not happen automatically when unpacking a release.

Check the applied policy:

```sh
sudo /usr/bin/python3 /usr/local/libexec/wifi-gpu/amd-memory-profile.py status --device /sys/bus/pci/devices/0000:01:00.0
```

The service applies at boot before Docker starts and restores the original
automatic policy when stopped. It does not restart after a driver reset;
check status and restart the unit while idle if the driver has reset its
policy. For native services, add ordering after this unit if they start at boot.

To remove the tuning while idle:

```sh
sudo systemctl disable --now wifi-gpu-memory-profile@0000:01:00.0.service
```

This fixed memory setting is validated for the documented HIP workload on this
card. It is an explicit per-host choice, not a global backend default for all
AMD cards or other workloads.
