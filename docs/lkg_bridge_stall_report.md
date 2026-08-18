# Bug report draft: DrawInteropQuiltTextureDX stalls 1-20 seconds (Bridge 2.6.3, Windows)

Draft for Looking Glass support / GitHub. Prepared 2026-08-18 from instrumented field data.

## Summary

`DrawInteropQuiltTextureDX` (called via `bridge_inproc.dll`, Bridge 2.6.3.0, Windows 11 Pro
10.0.26200) intermittently blocks the calling thread for between 0.7 and 20 seconds. Short
stalls cluster around 1.4s; long stalls cluster tightly at 19-20s, which looks like an internal
timeout constant. During a stall the Bridge service process (`LookingGlassBridge`, also 2.6.3)
is essentially idle (<3% CPU, stable thread count and memory), and a callstack captured mid-stall
shows the call parked in an ntdll wait inside bridge_inproc - i.e. a blocked synchronization
wait (IPC reply?), not GPU or CPU work.

## Environment

- Looking Glass Portrait, serial LKG-PORT-06955
- Bridge 2.6.3 (service + bridge_inproc.dll 2.6.3.0)
- Windows 11 Pro 10.0.26200, D3D11 interop path
- Caller: Unreal Engine 5.8 app using the open-source Unreal plugin's Bridge wrapper
  (ControllerWithCalibrationTemplates), one dedicated thread owns Initialize/InstanceWindowDX/
  RegisterTextureDX/DrawInteropQuiltTextureDX; draw rate <= 60 Hz; quilt is a 3360x3360
  PF_A16B16G16R16 render target, 8x6 tiles, registered once (handle never changes - verified,
  no re-registration events logged)

## Measured stalls (each verified as DrawInteropQuiltTextureDX by per-call timing)

| local time (2026-08) | duration |
|---|---|
| 17 ~22:20 | 18.97 s |
| 17 ~22:21 | 19.97 s |
| 18 08:10:15 | 1.53 s |
| 18 08:10:26 | 0.74 s |
| 18 08:10:47 | 1.85 s |
| 18 08:27:50 | 0.26 s |
| 18 08:28:32 | 19.75 s |
| 18 08:37:09 | 19.15 s |
| 18 09:00:48 | 1.37 s |
| 18 09:01:52 | 19.21 s |
| 18 09:07:36 | 1.40 s |

Reproduces unattended (no input, no window/focus changes, no display events in the Windows
event log at stall times; Bridge service log silent). Frequency: every few minutes of
continuous rendering.

## Mid-stall callstack of the calling thread

bridge_inproc.dll 2.6.3.0, base-relative offsets (module size 8847360):

```
ntdll.dll        (wait)
ntdll.dll
ntdll.dll
bridge_inproc.dll + 0x430F95
bridge_inproc.dll + 0x114294
bridge_inproc.dll + 0x3590ED
bridge_inproc.dll + 0x38F5B2
bridge_inproc.dll + 0x3581F0
bridge_inproc.dll + 0x35553C
bridge_inproc.dll + 0x35595E
bridge_inproc.dll + 0xC944C   <- entered from DrawInteropQuiltTextureDX
(caller)
```

## Impact

The calling thread is blocked for up to 20 seconds per occurrence; any app calling from its
render or game thread freezes entirely for that time. (We moved all Bridge calls to a dedicated
thread as a workaround, so only the hologram freezes, but the device still shows a frozen image
for the duration.)

## Questions

1. What is DrawInteropQuiltTextureDX waiting on internally, and what is the ~19-20s timeout?
2. Is there a way to make the call fail fast / asynchronous, or a recommended pattern that
   avoids the synchronous wait?
