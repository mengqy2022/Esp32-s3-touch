#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PC Monitor host agent
=====================
Pushes wall-clock time + CPU / GPU / MEM telemetry from this PC to the ESP32
device over the Type-C USB-serial port (the same console UART used by the
FILE: transfer tool). The device renders it on its new PC MONITOR screen
(big clock + load bars + temperatures).

Wire protocol (ASCII lines, one per line, sent TO the device):
    PC:NAME|<hostname>
    PC:TZOFF|<minutes east of UTC>      once at startup
    PC:TIME|<unix seconds>              every 15 s (keeps the clock accurate)
    PC:STAT|<cpu%>|<gpu%>|<cpu_C>|<gpu_C>|<mem%>|<used_MB>|<total_MB>

Usage:
    pip install pyserial
    python tools/pc_monitor_host.py                 # auto-detect COM port
    python tools/pc_monitor_host.py --port COM8
    python tools/pc_monitor_host.py --list-ports
    python tools/pc_monitor_host.py --interval 2.0  # slower updates

Notes:
  - CPU load/temperature are read with the Windows API (GetSystemTimes + WMI
    MSAcpi_ThermalZoneTemperature); MEM via GlobalMemoryStatusEx.
  - GPU figures use NVIDIA NVML (nvml.dll), with nvidia-smi as a fallback.
    Non-NVIDIA GPUs report -1 (the device shows "--").
  - On Linux, /proc is used for CPU/MEM; GPU falls back to nvidia-smi.
"""

import argparse
import ctypes
import datetime
import platform
import socket
import subprocess
import sys
import time

IS_WINDOWS = platform.system() == "Windows"

if sys.version_info >= (3, 0):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

BAUD = 115200

# ---------------------------------------------------------------------------
# CPU / MEM sampling (Windows: pure ctypes, no extra dependencies)
# ---------------------------------------------------------------------------
class WindowsPerf(object):
    """CPU busy-% (GetSystemTimes) and MEM load-% (GlobalMemoryStatusEx)."""

    def __init__(self):
        if not IS_WINDOWS:
            raise RuntimeError("Windows only")
        self._k32 = ctypes.windll.kernel32

        class FILETIME(ctypes.Structure):
            _fields_ = [("dwLow", ctypes.c_uint32), ("dwHigh", ctypes.c_uint32)]

        class MEMORYSTATUSEX(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_uint32),
                ("dwMemoryLoad", ctypes.c_uint32),
                ("ullTotalPhys", ctypes.c_uint64),
                ("ullAvailPhys", ctypes.c_uint64),
                ("ullTotalPageFile", ctypes.c_uint64),
                ("ullAvailPageFile", ctypes.c_uint64),
                ("ullTotalVirtual", ctypes.c_uint64),
                ("ullAvailVirtual", ctypes.c_uint64),
                ("ullAvailExtendedVirtual", ctypes.c_uint64),
            ]

        self.FILETIME = FILETIME
        self.MEMORYSTATUSEX = MEMORYSTATUSEX
        self._idle = self._kernel = self._user = None

    def _filetime_to_100ns(self, ft):
        return (ft.dwHigh << 32) | ft.dwLow

    def cpu_percent(self):
        idle, kernel, user = (
            self.FILETIME(), self.FILETIME(), self.FILETIME())
        if not self._k32.GetSystemTimes(
                ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            return -1
        i = self._filetime_to_100ns(idle)
        k = self._filetime_to_100ns(kernel)
        u = self._filetime_to_100ns(user)
        if self._idle is None:
            self._idle, self._kernel, self._user = i, k, u
            time.sleep(0.2)
            return self.cpu_percent()
        di, dk, du = i - self._idle, k - self._kernel, u - self._user
        self._idle, self._kernel, self._user = i, k, u
        total = dk + du
        if total <= 0:
            return -1
        return max(0, min(100, int(round(100.0 * (1.0 - di / total)))))

    def mem_info(self):
        st = self.MEMORYSTATUSEX()
        st.dwLength = ctypes.sizeof(st)
        if not self._k32.GlobalMemoryStatusEx(ctypes.byref(st)):
            return -1
        return int(st.dwMemoryLoad), int(st.ullTotalPhys // (1024*1024) - st.ullAvailPhys // (1024*1024)), int(st.ullTotalPhys // (1024*1024))

    def mem_percent(self):
        return self.mem_info()[0]


class LinuxPerf(object):
    """CPU busy-% and MEM load-% from /proc (no extra dependencies)."""

    def __init__(self):
        self._prev = None

    def cpu_percent(self):
        try:
            with open("/proc/stat", "r") as f:
                parts = f.readline().split()
            vals = [int(v) for v in parts[1:8]]
        except (OSError, ValueError, IndexError):
            return -1
        idle = vals[3] + vals[4]
        total = sum(vals)
        if self._prev is None:
            self._prev = (idle, total)
            time.sleep(0.2)
            return self.cpu_percent()
        di, dt = idle - self._prev[0], total - self._prev[1]
        self._prev = (idle, total)
        if dt <= 0:
            return -1
        return max(0, min(100, int(round(100.0 * (1.0 - di / dt)))))

    def mem_info(self):
        try:
            memo = {}
            with open("/proc/meminfo", "r") as f:
                for line in f:
                    key, _, rest = line.partition(":")
                    memo[key.strip()] = int(rest.split()[0])
            total = memo.get("MemTotal", 0)
            avail = memo.get("MemAvailable", memo.get("MemFree", 0))
            if total <= 0:
                return -1
            used = total - avail
            return int(round(100.0 * used / total)), int(used // 1024), int(total // 1024)
        except (OSError, ValueError):
            return -1, -1, -1

    def mem_percent(self):
        return self.mem_info()[0]


# ---------------------------------------------------------------------------
# CPU temperature
# Priority: LibreHardwareMonitor/OpenHardwareMonitor WMI shared sensors,
# fallback to ACPI ThermalZone.
# ---------------------------------------------------------------------------
class CpuTempProbe(object):
    def __init__(self):
        self._value = -1
        self._at = 0.0

    def read(self):
        now = time.time()
        if now - self._at < 15.0:
            return self._value
        self._at = now
        self._value = self._read_once()
        if self._value < 0:
            # Unavailable (no ACPI zone / no admin rights): retry slowly and
            # stay quiet instead of spamming the console.
            self._at = now + 105.0
        return self._value

    def _read_once(self):
        if not IS_WINDOWS:
            return -1
        ps = ("powershell.exe -NoProfile -NonInteractive -Command "
              "\"$x=Get-CimInstance -Namespace root/OpenHardwareMonitor -ClassName Sensor "
              "-ErrorAction SilentlyContinue | Where-Object {$_.SensorType -eq 'Temperature'} "
              "| Select-Object -First 1; if($x){$x.Value}else{"
              "(Get-CimInstance -Namespace root/wmi -ClassName MSAcpi_ThermalZoneTemperature "
              "| Select-Object -First 1).CurrentTemperature}\"")
        try:
            kw = {"creationflags": subprocess.CREATE_NO_WINDOW,
                  "stderr": subprocess.DEVNULL} if IS_WINDOWS else {
                  "stderr": subprocess.DEVNULL}
            out = subprocess.check_output(ps, shell=True, timeout=8, **kw)
            text = out.decode("utf-8", "replace").strip()
            val = float(text)
            if val <= 0:
                return -1
            return int(round(val / 10.0 - 273.15))
        except Exception:
            return -1


# ---------------------------------------------------------------------------
# GPU (NVIDIA NVML via ctypes; nvidia-smi as fallback)
# ---------------------------------------------------------------------------
class GpuProbe(object):
    def __init__(self):
        self._nvml = None
        self._handle = None
        self._smi_ok = True
        self._load_nvml()

    def _load_nvml(self):
        if not IS_WINDOWS:
            return
        try:
            lib = ctypes.windll.LoadLibrary("nvml.dll")
            lib.nvmlInit_v2.restype = ctypes.c_int
            if lib.nvmlInit_v2() != 0:
                return
            h = ctypes.c_void_p()
            lib.nvmlDeviceGetHandleByIndex_v2.restype = ctypes.c_int
            lib.nvmlDeviceGetHandleByIndex_v2.argtypes = [
                ctypes.c_uint, ctypes.POINTER(ctypes.c_void_p)]
            if lib.nvmlDeviceGetHandleByIndex_v2(0, ctypes.byref(h)) != 0:
                lib.nvmlShutdown()
                return

            class Util(ctypes.Structure):
                _fields_ = [("gpu", ctypes.c_uint), ("memory", ctypes.c_uint)]

            lib.nvmlDeviceGetUtilizationRates.restype = ctypes.c_int
            lib.nvmlDeviceGetUtilizationRates.argtypes = [
                ctypes.c_void_p, ctypes.POINTER(Util)]
            lib.nvmlDeviceGetTemperature.restype = ctypes.c_int
            lib.nvmlDeviceGetTemperature.argtypes = [
                ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_uint)]
            self._util_t = Util
            self._nvml = lib
            self._handle = h
        except Exception:
            self._nvml = None

    def read(self):
        if self._nvml and self._handle:
            u = self._util_t()
            t = ctypes.c_uint()
            try:
                if (self._nvml.nvmlDeviceGetUtilizationRates(
                        self._handle, ctypes.byref(u)) == 0 and
                        self._nvml.nvmlDeviceGetTemperature(
                            self._handle, 0, ctypes.byref(t)) == 0):
                    return int(u.gpu), int(t.value)
            except Exception:
                pass
        return self._read_smi()

    def _read_smi(self):
        if not self._smi_ok:
            return -1, -1
        cmd = ("nvidia-smi --query-gpu=utilization.gpu,temperature.gpu "
               "--format=csv,noheader,nounits")
        try:
            kw = {"creationflags": subprocess.CREATE_NO_WINDOW} if IS_WINDOWS else {}
            out = subprocess.check_output(cmd, shell=True, timeout=6, **kw)
            text = out.decode("utf-8", "replace").strip().splitlines()[0]
            parts = [p.strip() for p in text.split(",")]
            return int(parts[0]), int(parts[1])
        except Exception:
            self._smi_ok = False  # stop retrying: no NVIDIA GPU / driver
            return -1, -1


# ---------------------------------------------------------------------------
# Time helpers
# ---------------------------------------------------------------------------
def tz_offset_minutes():
    """Minutes east of UTC, e.g. +480 in China, -300 in New York (EST)."""
    try:
        delta = datetime.datetime.now().astimezone().utcoffset()
        if delta is None:
            return 0
        return int(delta.total_seconds() // 60)
    except Exception:
        return 0


def pick_default_port():
    """Prefer known ESP32 USB-UART bridge VIDs, else the single available port."""
    if serial is None:
        return None
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        return None
    for vid in (0x1A86, 0x10C4, 0x303A, 0x0403, 0x2341, 0x2A03):
        for p in ports:
            if p.vid == vid:
                return p.device
    if len(ports) == 1:
        return ports[0].device
    return ports[0].device


def list_ports():
    if serial is None:
        print("pyserial is missing: pip install pyserial")
        return
    for p in serial.tools.list_ports.comports():
        vid = ("%04X" % p.vid) if p.vid else "----"
        pid = ("%04X" % p.pid) if p.pid else "----"
        print(f"{p.device:<10} VID:PID {vid}:{pid}  {p.description}")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def connect(port):
    """Open the port; returns serial object or None after retries."""
    if serial is None:
        print("缺少 pyserial，请先安装：pip install pyserial")
        sys.exit(1)
    while True:
        try:
            ser = serial.Serial(port, BAUD, timeout=0.2, write_timeout=2)
            return ser
        except (serial.SerialException, OSError) as exc:
            print(f"[{port}] 打开失败: {exc} — 3 秒后重试 (Ctrl+C 退出)")
            time.sleep(3)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", "-p", help="串口名，例如 COM8 或 /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=BAUD, help="波特率 (默认 115200)")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="遥测发送间隔秒数 (默认 1.0)")
    ap.add_argument("--list-ports", action="store_true", help="列出可用串口后退出")
    args = ap.parse_args()

    if args.list_ports:
        list_ports()
        return

    port = args.port or pick_default_port()
    if not port:
        print("未找到串口。插好设备后重试，或用 --port 指定。")
        return

    hostname = socket.gethostname()[:23]
    print(f"== ESP32 PC Monitor host ==")
    print(f"   串口: {port} @ {args.baud}   更新间隔: {args.interval}s")
    print(f"   主机: {hostname}   时区偏移: {tz_offset_minutes()} 分钟 (UTC 东侧为正)")

    perf = WindowsPerf() if IS_WINDOWS else LinuxPerf()
    tmp = CpuTempProbe()
    gpu = GpuProbe()

    ser = connect(port)
    last_time_sync = 0.0
    first = True

    while True:
        try:
            cpu = perf.cpu_percent()
            mem, mem_used, mem_total = perf.mem_info()
            gpu_load, gpu_temp = gpu.read()
            cpu_temp = tmp.read()

            now = time.time()
            boot = first or now - last_time_sync >= 15.0
            first = False

            lines = []
            if first:
                lines.append(f"PC:TZOFF|{tz_offset_minutes()}")
                lines.append(f"PC:NAME|{hostname}")
                lines.append(f"PC:TIME|{int(now)}")
                last_time_sync = now
            elif now - last_time_sync >= 15.0:
                lines.append(f"PC:TIME|{int(now)}")
                last_time_sync = now
            lines.append(f"PC:STAT|{cpu}|{gpu_load}|{cpu_temp}|{gpu_temp}|{mem}|{mem_used}|{mem_total}")

            for line in lines:
                ser.write(line.encode("ascii") + b"\n")
            # Drain device log output (ESP_LOG / FILE: lines) so the host-side RX
            # buffer never fills up. Pass it through as "DEV| ..." so both
            # directions of the link stay visible on this console.
            try:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting)
                    for raw in data.decode("utf-8", "replace").splitlines():
                        line = raw.strip()
                        if line:
                            print("DEV| " + line, flush=True)
            except Exception:
                pass
            tags = [f"CPU {cpu:3d}%" if cpu >= 0 else "CPU  n/a",
                    f"GPU {gpu_load:3d}%" if gpu_load >= 0 else "GPU   --",
                    f"{cpu_temp:2d}C" if cpu_temp >= 0 else " --C ",
                    f"{gpu_temp:2d}C" if gpu_temp >= 0 else " --C ",
                    f"MEM {mem:3d}% {mem_used}/{mem_total}MB" if mem >= 0 else "MEM  n/a"]
            status = "  ".join(tags) + \
                ("   [TIME]" if boot else "") + \
                f"  {datetime.datetime.now():%H:%M:%S}"
            print(status, flush=True)
        except (serial.SerialException, OSError) as exc:
            print(f"串口断开: {exc} — 重连中...")
            try:
                ser.close()
            except Exception:
                pass
            ser = connect(port)
            first = True
            continue
        except KeyboardInterrupt:
            break

        try:
            time.sleep(max(0.1, args.interval - 0.3))
        except KeyboardInterrupt:
            break

    try:
        ser.close()
    except Exception:
        pass
    print("bye")


if __name__ == "__main__":
    main()