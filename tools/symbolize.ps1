# Resolves module RVAs (as printed by tools\dump_threads.py) to symbol names and
# source lines using dbghelp + the module's PDB. No windbg needed.
# Usage:
#   tools\symbolize.ps1 -Rvas 0x5D0F304,0x12B65F8
#   tools\symbolize.ps1 -Image <path\to\module.exe_or_dll> -Rvas 0x1234
# Default image is the staged LKG Shipping exe (its .pdb sits next to it).
param(
    [Parameter(Mandatory=$true)][string[]]$Rvas,
    [string]$Image = "$PSScriptRoot\..\dist\win64_lkg_test\Windows\HoloVCS\Binaries\Win64\HoloVCSLKG-Win64-Shipping.exe"
)

$Image = (Resolve-Path $Image).Path

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class Sym
{
    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymInitialize(IntPtr hProcess, string UserSearchPath, bool fInvadeProcess);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern uint SymSetOptions(uint SymOptions);

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern ulong SymLoadModuleEx(IntPtr hProcess, IntPtr hFile, string ImageName,
        string ModuleName, ulong BaseOfDll, uint DllSize, IntPtr Data, uint Flags);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct SYMBOL_INFO
    {
        public uint SizeOfStruct;
        public uint TypeIndex;
        public ulong Reserved1;
        public ulong Reserved2;
        public uint Index;
        public uint Size;
        public ulong ModBase;
        public uint Flags;
        public ulong Value;
        public ulong Address;
        public uint Register;
        public uint Scope;
        public uint Tag;
        public uint NameLen;
        public uint MaxNameLen;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)]
        public string Name;
    }

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymFromAddr(IntPtr hProcess, ulong Address, out ulong Displacement, ref SYMBOL_INFO Symbol);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct IMAGEHLP_LINE64
    {
        public uint SizeOfStruct;
        public IntPtr Key;
        public uint LineNumber;
        public IntPtr FileName;
        public ulong Address;
    }

    [DllImport("dbghelp.dll", SetLastError = true)]
    public static extern bool SymGetLineFromAddr64(IntPtr hProcess, ulong Address, out uint Displacement, ref IMAGEHLP_LINE64 Line);
}
'@

$h = [IntPtr]::new(1)
[void][Sym]::SymSetOptions(0x2 -bor 0x10)  # UNDNAME | LOAD_LINES
if (-not [Sym]::SymInitialize($h, (Split-Path $Image), $false)) {
    throw "SymInitialize failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}
$base = [Sym]::SymLoadModuleEx($h, [IntPtr]::Zero, $Image, $null, 0x140000000, 0, [IntPtr]::Zero, 0)
if ($base -eq 0) { throw "SymLoadModuleEx failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }

foreach ($r in $Rvas) {
    $rva = [Convert]::ToUInt64($r, 16)
    $addr = $base + $rva
    $si = New-Object Sym+SYMBOL_INFO
    $si.SizeOfStruct = 88
    $si.MaxNameLen = 1024
    $disp = [uint64]0
    if ([Sym]::SymFromAddr($h, $addr, [ref]$disp, [ref]$si)) {
        $line = ""
        $li = New-Object Sym+IMAGEHLP_LINE64
        $li.SizeOfStruct = [Runtime.InteropServices.Marshal]::SizeOf([type][Sym+IMAGEHLP_LINE64])
        $ld = [uint32]0
        if ([Sym]::SymGetLineFromAddr64($h, $addr, [ref]$ld, [ref]$li)) {
            $fn = [Runtime.InteropServices.Marshal]::PtrToStringAnsi($li.FileName)
            $line = "  [$fn : $($li.LineNumber)]"
        }
        "{0}  {1}+0x{2:X}{3}" -f $r, $si.Name, $disp, $line
    } else {
        "{0}  <no symbol>" -f $r
    }
}
