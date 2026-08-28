# Writes a minidump of a LIVE process (e.g. a hung game) for tools\dump_threads.py.
# Usage: tools\writedump.ps1 -ProcId 1234 -Path out.dmp [-Full]
# Default dump type is small (stacks + thread info); -Full captures all memory (big).
# Note: rundll32 comsvcs.dll MiniDump silently writes 0-byte files on this machine;
# this direct MiniDumpWriteDump call is the one that works.
param(
    [Parameter(Mandatory=$true)][int]$ProcId,
    [Parameter(Mandatory=$true)][string]$Path,
    [switch]$Full
)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MD
{
    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool MiniDumpWriteDump(IntPtr hProcess, uint ProcessId, IntPtr hFile,
        uint DumpType, IntPtr ExceptionParam, IntPtr UserStreamParam, IntPtr CallbackParam);
}
'@
$p = Get-Process -Id $ProcId
$fs = [System.IO.File]::Create($Path)
# 0x0 Normal | 0x400 WithThreadInfo | 0x4 WithHandleData; 0x2 = WithFullMemory
$type = if ($Full) { 0x2 -bor 0x400 } else { 0x0 -bor 0x400 -bor 0x4 }
$ok = [MD]::MiniDumpWriteDump($p.Handle, [uint32]$ProcId, $fs.SafeFileHandle.DangerousGetHandle(), $type, [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero)
$err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
$fs.Close()
if ($ok) { "dump written: $Path ($((Get-Item $Path).Length) bytes)" } else { "FAILED err $err" }
