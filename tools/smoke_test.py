#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Quick end-to-end smoke test of the FILE: serial protocol on COM4."""
import re
import sys
import time

import serial

BAUD = 115200
CMD_TIMEOUT = 5.0
RX_CHUNK = 256


class DeviceLink:
    def __init__(self, port):
        self.ser = serial.Serial(port, BAUD, timeout=0.2, write_timeout=2)
        self.buf = b""

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _read_line(self, timeout=CMD_TIMEOUT):
        end = time.time() + timeout
        while time.time() < end:
            n = self.ser.in_waiting
            if n > 0:
                data = self.ser.read(min(n, RX_CHUNK))
                self.buf += data
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                line = line.rstrip(b"\r").decode("utf-8", "replace").strip()
                if not line:
                    continue
                if re.match(r"^[IEW]\s*\(\d+\)", line):
                    continue
                return line
            time.sleep(0.02)
        return None

    def send(self, s):
        self.ser.write((s + "\n").encode("utf-8"))

    def ping(self):
        self.send("FILE:PING")
        return self._read_line() == "FILE:PONG"

    def list_dir(self, path="/sdcard"):
        self.send(f"FILE:LIST|{path}")
        entries = []
        while True:
            line = self._read_line()
            if line is None:
                return entries, "timeout"
            if line.startswith("FILE:ERR|"):
                return entries, line[8:]
            if line == "FILE:DONE":
                return entries, None
            if line.startswith("FILE:ENTRY|"):
                parts = line[11:].split("|")
                if len(parts) >= 3:
                    entries.append((parts[0], parts[1], parts[2]))
                else:
                    entries.append((line[11:], "?", "?"))

    def put(self, path, data):
        self.send(f"FILE:PUT|{path}|{len(data)}")
        line = self._read_line()
        if line != "FILE:READY":
            return False, line or "no ready"
        self.ser.write(data)
        self.ser.flush()
        resp = self._read_line(CMD_TIMEOUT + len(data) / 115200.0 * 2)
        if resp == "FILE:OK":
            return True, "OK"
        return False, resp or "timeout"

    def _read_raw(self, n, timeout):
        """Read exactly n raw bytes, draining the line-buffer stash first."""
        end = time.time() + timeout
        out = bytearray()
        while len(out) < n and time.time() < end:
            if self.buf:
                take = self.buf[:n - len(out)]
                self.buf = self.buf[len(take):]
                out += take
            else:
                avail = self.ser.in_waiting
                if avail > 0:
                    chunk = self.ser.read(min(avail, n - len(out)))
                    out += chunk
                else:
                    time.sleep(0.02)
        return bytes(out)

    def get(self, path):
        self.send(f"FILE:GET|{path}")
        line = self._read_line()
        if not line.startswith("FILE:READY|"):
            return False, b"", line or "no ready"
        size = int(line.split("|")[1])
        data = self._read_raw(size, CMD_TIMEOUT + size / 115200.0 * 3)
        if len(data) != size:
            return False, data, f"short read {len(data)}/{size}"
        self._read_line(2)
        return True, data, "OK"

    def delete(self, path):
        self.send(f"FILE:DEL|{path}")
        resp = self._read_line()
        return resp == "FILE:OK", resp or "timeout"


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"
    link = DeviceLink(port)
    try:
        print("== PING ==")
        print("PONG" if link.ping() else "NO RESPONSE")

        print("\n== LIST /sdcard ==")
        entries, err = link.list_dir("/sdcard")
        print("err:", err)
        for path, size, kind in entries:
            print(f"  {path}  {size} {kind}")

        print("\n== PUT small file ==")
        ok, msg = link.put("/sdcard/SMOKE.TXT", b"hello from pc\nline2\n")
        print("put:", ok, msg)

        print("\n== GET it back ==")
        ok, data, msg = link.get("/sdcard/SMOKE.TXT")
        print("get:", ok, repr(data), msg)

        print("\n== DEL it ==")
        ok, msg = link.delete("/sdcard/SMOKE.TXT")
        print("del:", ok, msg)

        print("\nSMOKE TEST DONE")
    finally:
        link.close()


if __name__ == "__main__":
    main()
