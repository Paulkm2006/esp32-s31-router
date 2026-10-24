# SPDX-License-Identifier: Apache-2.0
# PowerShell 7, Windows 11 24H2+, with storage_host/router_smb_host running on localhost.
param(
    [ValidateRange(1024, 65535)][int]$Port = 1445,
    [ValidatePattern('^[D-Z]:$')][string]$Drive = 'Y:'
)
$ErrorActionPreference = 'Stop'
$remote = '\\127.0.0.1\usb'
if (Get-PSDrive -Name $Drive.TrimEnd(':') -ErrorAction SilentlyContinue) { throw 'Test drive is already in use' }
& net.exe use $Drive $remote '测试-Password1!' /user:testuser "/TCPPORT:$Port" /persistent:no
if ($LASTEXITCODE) { throw "Native SMB mapping failed: $LASTEXITCODE" }
$folder = "$Drive\windows-native-$([guid]::NewGuid().ToString('N'))"
$source = Join-Path $folder '原生读写.bin'
$renamed = Join-Path $folder '重命名.bin'
try {
    if ((Get-SmbMapping -LocalPath $Drive).RemotePath -ne $remote) { throw 'Unexpected test mapping' }
    [IO.Directory]::CreateDirectory($folder) | Out-Null
    [byte[]]$bytes = New-Object byte[] 262144
    for ($i = 0; $i -lt $bytes.Length; $i++) { $bytes[$i] = $i % 251 }
    [IO.File]::WriteAllBytes($source, $bytes)
    $actual = [IO.File]::ReadAllBytes($source)
    if ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($actual)) -ne
        [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))) { throw 'Native SMB readback mismatch' }
    [IO.File]::Move($source, $renamed)
    $files = [IO.Directory]::GetFiles($folder)
    if ($files.Length -ne 1 -or [IO.Path]::GetFileName($files[0]) -ne '重命名.bin') { throw 'Native directory listing mismatch' }
    [IO.File]::Delete($renamed)
    [IO.Directory]::Delete($folder)
    'Windows native SMB mount, 256 KiB read/write, UTF-8, rename, listing and delete: passed'
} finally {
    if ([IO.File]::Exists($source)) { [IO.File]::Delete($source) }
    if ([IO.File]::Exists($renamed)) { [IO.File]::Delete($renamed) }
    if ([IO.Directory]::Exists($folder)) { [IO.Directory]::Delete($folder) }
    & net.exe use $Drive /delete /y | Out-Null
}
