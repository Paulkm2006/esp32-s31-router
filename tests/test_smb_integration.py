# SPDX-License-Identifier: Apache-2.0
"""Run against storage_host/router_smb_host on port 1445. Requires smbprotocol."""
import os
import uuid

import smbclient
from smbprotocol.connection import Connection
from smbprotocol import Dialects
from smbprotocol.exceptions import LogonFailure, ObjectNameNotFound, SMBResponseException
from smbprotocol.open import SMB2CreateRequest, SMB2CloseRequest

SERVER = os.environ.get("SMB_TEST_SERVER", "127.0.0.1")
ROOT = rf"\\{SERVER}\usb"
KW = {"port": 1445}


def main() -> None:
    try:
        smbclient.register_session(SERVER, username="testuser", password="wrong-password", port=1445,
                                   require_signing=True, connection_timeout=5)
    except LogonFailure:
        pass
    else:
        raise AssertionError("Incorrect password was accepted")
    smbclient.reset_connection_cache()
    session = smbclient.register_session(SERVER, username="testuser", password="测试-Password1!", port=1445,
                                         require_signing=True, connection_timeout=5)
    assert session.connection.dialect >= Dialects.SMB_3_0_0
    assert session.connection.require_signing
    assert min(session.connection.max_transact_size, session.connection.max_read_size,
               session.connection.max_write_size) >= 65536
    print(f"Authenticated signed SMB dialect {session.connection.dialect:#x}", flush=True)
    folder = ROOT + r"\测试目录"
    smbclient.mkdir(folder, **KW)
    block_path = folder + r"\block.bin"
    block = bytes(range(256)) * 256
    with smbclient.open_file(block_path, mode="wb", **KW) as stream:
        stream.write(block)
    with smbclient.open_file(block_path, mode="rb", **KW) as stream:
        assert stream.read() == block
    smbclient.remove(block_path, **KW)
    path = folder + r"\文件.txt"
    with smbclient.open_file(path, mode="wb", **KW) as stream:
        stream.write(b"hello")
        stream.seek(4096)
        stream.write(b"end")
        stream.flush()
    assert smbclient.stat(path, **KW).st_size == 4099
    assert smbclient.stat(path, **KW).st_ino == smbclient.stat(path, **KW).st_ino
    with smbclient.open_file(path, mode="rb", **KW) as stream:
        assert stream.read() == b"hello" + bytes(4091) + b"end"
    with smbclient.open_file(path, mode="r+b", **KW) as stream:
        stream.truncate(3)
    with smbclient.open_file(path, mode="rb", **KW) as stream:
        assert stream.read() == b"hel"
    with smbclient.open_file(path, mode="rb", buffering=0, **KW) as source:
        missing = SMB2CreateRequest()
        missing["impersonation_level"] = 2
        missing["desired_access"] = 1
        missing["share_access"] = 7
        missing["create_disposition"] = 1
        missing["create_options"] = 0x40
        missing["buffer_path"] = "测试目录\\does-not-exist".encode("utf-16-le")
        close = SMB2CloseRequest()
        close["file_id"] = bytes([255]) * 16
        tree = source.fd.tree_connect
        requests = source.fd.connection.send_compound([missing, close], tree.session.session_id,
                                                       tree.tree_connect_id, related=True)
        try:
            source.fd.connection.receive(requests[0])
        except ObjectNameNotFound:
            pass
        else:
            raise AssertionError("Missing CREATE unexpectedly succeeded")
        try:
            source.fd.connection.receive(requests[1])
        except SMBResponseException:
            pass
        assert source.read() == b"hel", "A failed related CREATE/CLOSE must not close an unrelated handle"
    renamed = folder + r"\重命名.txt"
    smbclient.rename(path, renamed, **KW)
    assert smbclient.listdir(folder, **KW) == ["重命名.txt"]
    expected = {"重命名.txt"}
    for i in range(90):
        name = f"{i:03d}" + "长文件名" * 30 + ".txt"
        expected.add(name)
        with smbclient.open_file(folder + "\\" + name, mode="wb", **KW) as stream:
            stream.write(bytes([i]))
    names = smbclient.listdir(folder, **KW)
    assert set(names) == expected, "UTF-8 names must survive directory enumeration without short-name substitution"
    for name in names:
        smbclient.remove(folder + "\\" + name, **KW)
    smbclient.rmdir(folder, **KW)
    assert smbclient.listdir(ROOT, **KW) == []
    smbclient.reset_connection_cache()
    old = Connection(uuid.uuid4(), SERVER, port=1445)
    try:
        old.connect(dialect=Dialects.SMB_2_1_0, timeout=5)
    except Exception:
        pass
    else:
        raise AssertionError("SMB2 was accepted by the SMB3-only service")
    finally:
        old.disconnect()
    print("SMB integration passed: authentication, signing, 64 KiB transfer, UTF-8, read/write, zero-fill, truncate, failed CREATE/CLOSE isolation, rename, paginated listing, delete and SMB2 rejection.")


if __name__ == "__main__":
    main()
