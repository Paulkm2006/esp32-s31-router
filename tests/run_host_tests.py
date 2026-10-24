# SPDX-License-Identifier: Apache-2.0
"""Compile and run host tests using CC, cc, or an installed MSVC toolchain."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
IDF = Path(os.environ.get("IDF_PATH", "D:/esp/v6.1/esp-idf"))


def main() -> None:
    env = os.environ.copy()
    compiler = env.get("CC") or shutil.which("cc") or shutil.which("cl")
    if compiler is None and os.name == "nt":
        installer = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output(
            [str(installer), "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-latest", "-property", "installationPath"], text=True,
        ).strip()
        toolset = sorted((Path(installation) / "VC/Tools/MSVC").iterdir())[-1]
        sdk = Path(os.environ["ProgramFiles(x86)"]) / "Windows Kits/10"
        sdk_version = sorted((sdk / "Include").iterdir())[-1].name
        compiler = str(toolset / "bin/Hostx64/x64/cl.exe")
        env["INCLUDE"] = ";".join(str(path) for path in [
            toolset / "include", sdk / "Include" / sdk_version / "ucrt",
        ])
        env["LIB"] = ";".join(str(path) for path in [
            toolset / "lib/x64", sdk / "Lib" / sdk_version / "ucrt/x64", sdk / "Lib" / sdk_version / "um/x64",
        ])
    if compiler is None:
        raise SystemExit("Set CC to a C compiler; Windows also supports MSVC installed with Build Tools.")
    msvc = Path(compiler).name.lower() in {"cl", "cl.exe"}
    cjson = ROOT / "managed_components/espressif__cjson/cJSON"
    includes = [ROOT / "main", ROOT / "tests/host", cjson, IDF / "components/esp_common/include",
                IDF / "components/nvs_flash/include", ROOT / "build/config"]
    tests = {"subnet": [ROOT / "tests/test_subnet.c"],
             "config": [ROOT / "tests/test_config.c", ROOT / "main/config/router_config.c",
                        ROOT / "main/storage/router_storage_config.c"],
             "auth": [ROOT / "tests/test_auth.c", ROOT / "main/web/router_auth.c"],
             "ipv6": [ROOT / "tests/test_ipv6.c", ROOT / "main/network/router_ipv6.c"],
             "log": [ROOT / "tests/test_log.c", ROOT / "main/system/router_log.c", cjson / "cJSON.c"]}
    with tempfile.TemporaryDirectory(prefix="router-host-tests-") as temp:
        for name, sources in tests.items():
            test_includes = ([ROOT / "tests/host" / name] if name in {"ipv6", "log"} else []) + includes
            output = Path(temp) / (name + (".exe" if os.name == "nt" else ""))
            if msvc:
                args = ["/nologo", "/std:c17", "/W4", "/WX", "/MT", "/D_CRT_SECURE_NO_WARNINGS",
                        f"/FI{ROOT / 'tests/host/compiler.h'}", f"/Fe:{output}",
                        *[f"/I{path}" for path in test_includes]]
            else:
                args = ["-std=c17", "-Wall", "-Wextra", "-Werror", "-D_POSIX_C_SOURCE=200809L",
                        "-include", str(ROOT / "tests/host/compiler.h"),
                        "-o", str(output), *[f"-I{path}" for path in test_includes]]
            subprocess.run([compiler, *args, *map(str, sources)], env=env, cwd=temp, check=True)
            subprocess.run([str(output)], cwd=temp, check=True)
            if name == "ipv6":
                subprocess.run([str(output), "no-multicast"], cwd=temp, check=True)
            print(f"{name}: passed", flush=True)


if __name__ == "__main__":
    main()
