param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'
$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($resolvedPath)

if ($bytes.Length -lt 0x40) {
    throw "File is too small to be a PE executable: $resolvedPath"
}

$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 0 -or $peOffset + 24 -gt $bytes.Length) {
    throw "Invalid PE header offset in $resolvedPath"
}

if ($bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
    $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
    throw "PE signature was not found in $resolvedPath"
}

$optionalHeaderOffset = $peOffset + 24
$magic = [BitConverter]::ToUInt16($bytes, $optionalHeaderOffset)
if ($magic -eq 0x20b) {
    $dataDirectoryOffset = $optionalHeaderOffset + 112
}
elseif ($magic -eq 0x10b) {
    $dataDirectoryOffset = $optionalHeaderOffset + 96
}
else {
    throw "Unsupported PE optional-header magic 0x$('{0:X}' -f $magic) in $resolvedPath"
}

# IMAGE_DIRECTORY_ENTRY_SECURITY is data-directory index 4. Its VirtualAddress is a file offset.
$securityDirectoryOffset = $dataDirectoryOffset + (4 * 8)
if ($securityDirectoryOffset + 8 -gt $bytes.Length) {
    throw "PE security directory is outside the file header in $resolvedPath"
}

$certificateOffset = [BitConverter]::ToUInt32($bytes, $securityDirectoryOffset)
$certificateSize = [BitConverter]::ToUInt32($bytes, $securityDirectoryOffset + 4)
$certificateEnd = [uint64]$certificateOffset + [uint64]$certificateSize

if ($certificateOffset -eq 0 -and $certificateSize -eq 0) {
    Write-Output "PE certificate table is already empty: $resolvedPath"
    exit 0
}

if ($certificateOffset -le $bytes.Length -and $certificateEnd -le $bytes.Length) {
    Write-Output "PE certificate table is valid and was not changed: $resolvedPath"
    exit 0
}

[Array]::Copy([BitConverter]::GetBytes([uint32]0), 0, $bytes, $securityDirectoryOffset, 4)
[Array]::Copy([BitConverter]::GetBytes([uint32]0), 0, $bytes, $securityDirectoryOffset + 4, 4)
[System.IO.File]::WriteAllBytes($resolvedPath, $bytes)

Write-Output "Cleared invalid out-of-bounds PE certificate table: $resolvedPath"
