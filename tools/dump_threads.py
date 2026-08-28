# Prints every thread's stack from a Windows user-mode minidump as module+RVA frames,
# plus the exception record if the dump has one. No windbg needed.
# Usage: python tools\dump_threads.py <dump.dmp> [all]
#   'all' prints every thread's frames; default only prints threads whose stack
#   touches the game/core modules (others get a one-line RIP summary).
# Resolve the HoloVCSLKG RVAs it prints with tools\symbolize.ps1.
# Needs: pip install minidump
# WER crash dumps land in %LOCALAPPDATA%\CrashDumps (per-process .dmp named with the pid).
# Note: this is a stack SCAN (every stack value that lands inside a module), so expect
# some stale/false frames mixed in with the real call chain.
import struct
import sys

from minidump.minidumpfile import MinidumpFile

if len(sys.argv) < 2:
    print(__doc__ or "usage: dump_threads.py <dump.dmp> [all]")
    sys.exit(1)
DUMP = sys.argv[1]
SHOW_ALL = len(sys.argv) > 2 and sys.argv[2] == 'all'

mf = MinidumpFile.parse(DUMP)

mods = []
for m in mf.modules.modules:
    mods.append((m.baseaddress, m.baseaddress + m.size, m.name.split('\\')[-1]))
mods.sort()

def find_mod(addr):
    for lo, hi, name in mods:
        if lo <= addr < hi:
            return name, addr - lo
    return None, None

if mf.exception and mf.exception.exception_records:
    er = mf.exception.exception_records[0]
    n, o = find_mod(er.ExceptionRecord.ExceptionAddress)
    where = "%s+0x%X" % (n, o) if n else hex(er.ExceptionRecord.ExceptionAddress)
    print("EXCEPTION %s in thread %d at %s" % (er.ExceptionRecord.ExceptionCode, er.ThreadId, where))
    print()

reader = mf.get_reader()
bufreader = reader.get_buffered_reader()

INTEREST = ('HoloVCS', 'fceumm', 'stella', 'beetle', 'azahar', 'bridge')

for t in mf.threads.threads:
    buff = mf.file_handle
    buff.seek(t.ThreadContext.Rva)
    ctx = buff.read(t.ThreadContext.DataSize)
    # x64 CONTEXT offsets: Rsp 0x98, Rip 0xF8
    rip = struct.unpack_from('<Q', ctx, 0xF8)[0]
    rsp = struct.unpack_from('<Q', ctx, 0x98)[0]
    n, o = find_mod(rip)
    ripstr = "%s+0x%X" % (n, o) if n else hex(rip)
    frames = []
    addr = rsp
    for _ in range(0, 0x3000, 8):
        try:
            bufreader.move(addr)
            val = struct.unpack_from('<Q', bufreader.read(8), 0)[0]
        except Exception:
            addr += 8
            continue
        name, off = find_mod(val)
        if name:
            frames.append("%s+0x%X" % (name, off))
        addr += 8
    out = []
    for f in frames:
        if not out or out[-1] != f:
            out.append(f)
    has_interest = any(any(k.lower() in f.lower() for k in INTEREST) for f in out) or (n and 'HoloVCS' in n)
    tag = "  <== GAME/CORE FRAMES" if has_interest else ""
    print("TID %6d  RIP %-40s%s" % (t.ThreadId, ripstr, tag))
    if has_interest or SHOW_ALL:
        for f in out[:40]:
            print("      ", f)
    print()
