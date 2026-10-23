# SPDX-License-Identifier: Apache-2.0
"""Hardware smoke test; full Wi-Fi client/NAT acceptance is documented in README."""
import ipaddress
import platform
import subprocess

import pytest
from pytest_embedded import Dut


@pytest.mark.parametrize("target", ["esp32s31"], indirect=True)
def test_router_wan_ready(dut: Dut) -> None:
    match = dut.expect(
        r"router: WAN IP: (\d+\.\d+\.\d+\.\d+) mask: (\d+\.\d+\.\d+\.\d+) gateway: (\d+\.\d+\.\d+\.\d+)",
        timeout=90,
    )
    wan_ip, wan_mask, gateway = (value.decode() for value in match.groups())
    assert not ipaddress.ip_address(wan_ip).is_unspecified
    assert not ipaddress.ip_address(gateway).is_unspecified
    lan = dut.expect(r"router_ap: AP gateway: (\d+\.\d+\.\d+\.\d+), DNS: (\d+\.\d+\.\d+\.\d+)", timeout=30)
    lan_ip, dns = (value.decode() for value in lan.groups())
    assert not ipaddress.ip_network(f"{wan_ip}/{wan_mask}", strict=False).overlaps(
        ipaddress.ip_network(f"{lan_ip}/24", strict=False)
    )
    assert not ipaddress.ip_address(dns).is_unspecified
    dut.expect_exact("Internet forwarding ready (Ethernet -> Wi-Fi AP)", timeout=30)
    count_flag = "-n" if platform.system() == "Windows" else "-c"
    subprocess.run(["ping", count_flag, "1", wan_ip], check=True, capture_output=True, timeout=10)
