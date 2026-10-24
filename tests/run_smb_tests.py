# SPDX-License-Identifier: Apache-2.0
"""Run the native SMB/FatFs fixture and an independent signed SMB3 client on Linux."""
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    binary = ROOT / ".cache/smb-host/router_smb_host"
    with (ROOT / ".cache/smb-host-server.log").open("w") as log:
        server = subprocess.Popen([str(binary)], stdout=log, stderr=log)
        try:
            for _ in range(50):
                if server.poll() is not None:
                    raise RuntimeError("Fixture exited; inspect .cache/smb-host-server.log")
                try:
                    with socket.create_connection(("127.0.0.1", 1445), timeout=0.1):
                        break
                except OSError:
                    time.sleep(0.1)
            else:
                raise TimeoutError("SMB fixture did not listen")
            subprocess.run([sys.executable, str(ROOT / "tests/test_smb_integration.py")], check=True, timeout=120)
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
        if server.returncode:
            raise RuntimeError(f"Fixture failed with exit code {server.returncode}")


if __name__ == "__main__":
    main()
