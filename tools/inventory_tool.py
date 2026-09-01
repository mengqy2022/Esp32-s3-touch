#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Inventory / SD Card File Transfer Tool
轻量级串口文件传输小工具：通过串口连接 ESP32 设备，
直接在 PC 与设备 SD 卡之间传输 INVENTORY.CSV / BOM 文件，
无需拔出 SD 卡。

功能：
  - 上传 / 下载 / 删除设备 SD 卡文件
  - 清空库存（三次确认）
  - 导入 CSV 入库：自动识别三种格式并合并到设备库存
      1) 立创商城订单导出
      2) 嘉立创 EDA 原理图物料清单（BOM）
      3) 标准库存格式（LCSC,NAME,SPEC,PACKAGE,QTY，列名可中英混排）

依赖：pip install pyserial
运行：python inventory_tool.py
"""

import csv
import io
import os
import re
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None

BAUD = 115200
CMD_TIMEOUT = 5.0
RX_CHUNK = 256

INV_FILE = "/sdcard/INVENTORY.CSV"

# Device-side field limits (must match inventory.h)
LCSC_MAX, NAME_MAX, SPEC_MAX, PKG_MAX = 32, 32, 40, 20


# ---------------------------------------------------------------------------
# CSV decoding / column detection (mirrors the firmware logic in inventory.c)
# ---------------------------------------------------------------------------

def decode_bytes(raw):
    """Decode CSV bytes: UTF-8 (with/without BOM), UTF-16 (LE BOM), else GB18030."""
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16", errors="replace")
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("gb18030", errors="replace")


def strip_cell(s):
    return (s or "").strip().strip('\ufeff').strip('"').strip()


def norm_key(s):
    return re.sub(r"\s+", "", (s or "").lower())


KW_QTY = ["qty", "quantity", "count", "数量", "订购数量"]
KW_LCSC = ["lcsc", "lcsc part#", "编号", "part#", "商品编号", "供应商编号", "supplier part"]
KW_NAME = ["name", "value", "part", "型号", "名称", "comment", "商品名称", "物料名称"]
KW_SPEC = ["spec", "specification", "description", "规格", "描述", "厂家型号", "制造商型号", "manufacturer part"]
KW_PKG = ["package", "footprint", "封装"]

# 分级评分：目标列, 关键词(norm后), 得分。同一列取得分最高者，同分取后列。
COL_RULES = [
    ("qty", "订购数量", 4), ("qty", "quantity", 4), ("qty", "数量", 3),
    ("qty", "qty", 2), ("qty", "count", 2),
    ("lcsc", "商品编号", 4), ("lcsc", "supplierpart", 4), ("lcsc", "供应商编号", 4),
    ("lcsc", "lcsc", 4), ("lcsc", "part#", 2), ("lcsc", "编号", 2),
    ("name", "商品名称", 4), ("name", "物料名称", 4), ("name", "comment", 3),
    ("name", "value", 2), ("name", "name", 2), ("name", "型号", 1), ("name", "名称", 1),
    ("spec", "厂家型号", 4), ("spec", "制造商型号", 4), ("spec", "manufacturerpart", 4),
    ("spec", "spec", 3), ("spec", "规格", 3), ("spec", "description", 2), ("spec", "描述", 2),
    ("pkg", "package", 3), ("pkg", "footprint", 3), ("pkg", "封装", 3),
]


def detect_columns(header):
    """分级匹配列：返回 {qty,lcsc,name,spec,pkg} -> 列索引 或 -1。"""
    best = {t: (-1, -1) for t in ("qty", "lcsc", "name", "spec", "pkg")}  # (score, idx)
    for i, cell in enumerate(header):
        key = norm_key(cell)
        for target, kw, score in COL_RULES:
            if kw in key and score > best[target][0]:
                best[target] = (score, i)
    return {t: v[1] for t, v in best.items()}


def parse_int(s):
    m = re.search(r"-?\d+", s or "")
    return int(m.group()) if m else 0


def sanitize_field(s, max_len):
    """Remove characters the device CSV parser cannot handle (commas, quotes, newlines)."""
    s = (s or "").strip()
    s = s.replace('"', " ").replace(",", " ").replace("\r", " ").replace("\n", " ")
    s = re.sub(r"\s+", " ", s).strip()
    return s[:max_len]


def sniff_delimiter(text):
    """Pick ',' or '\\t' based on which appears more often in the first lines."""
    head = "\n".join(text.splitlines()[:10])
    return "\t" if head.count("\t") > head.count(",") else ","


def parse_rows_from_reader(reader, min_cols=2):
    """Yield data rows (lists) skipping blank / obvious header rows."""
    for row in reader:
        if not row:
            continue
        if len(row) < min_cols:
            continue
        if all(not (c or "").strip() for c in row):
            continue
        yield [strip_cell(c) for c in row]


def detect_format(rows):
    """Return 'lcsc_order' | 'eda_bom' | 'standard' | None."""
    # 立创商城订单的表头可能在文件中间（前面是订单信息），需扫描全部行
    for row in rows:
        joined = "|".join(norm_key(c) for c in row)
        if "商品编号" in joined and ("订购数量" in joined or "厂家型号" in joined):
            return "lcsc_order"
    for row in rows[:6]:
        keys = [norm_key(c) for c in row]
        if any("quantity" in k for k in keys) and \
           any("supplierpart" in k or k == "supplier" or "lcsc" in k for k in keys):
            return "eda_bom"
    for row in rows[:6]:
        cols = detect_columns(row)
        if cols["qty"] >= 0 and (cols["name"] >= 0 or cols["lcsc"] >= 0):
            return "standard"
    return None


def parse_lcsc_order(rows):
    """立创商城订单：找到商品明细表头行，提取编号/厂家型号/封装/名称/数量。"""
    items = []
    header_idx = None
    for i, row in enumerate(rows):
        key = "|".join(norm_key(c) for c in row)
        if "商品编号" in key or "订购数量" in key:
            header_idx = i
            break
    if header_idx is None:
        return items
    cols = detect_columns(rows[header_idx])
    if cols["qty"] < 0 or cols["lcsc"] < 0:
        return items
    for row in rows[header_idx + 1:]:
        if len(row) <= max(cols.values()):
            continue
        lcsc = sanitize_field(row[cols["lcsc"]], LCSC_MAX)
        if not re.match(r"^C\d+$", lcsc, re.IGNORECASE):
            continue
        qty = parse_int(row[cols["qty"]])
        name = sanitize_field(row[cols["name"]] if cols["name"] >= 0 else "", NAME_MAX)
        spec = sanitize_field(row[cols["spec"]] if cols["spec"] >= 0 else "", SPEC_MAX)
        pkg = sanitize_field(row[cols["pkg"]] if cols["pkg"] >= 0 else "", PKG_MAX)
        items.append({"lcsc": lcsc, "name": name, "spec": spec, "pkg": pkg, "qty": qty})
    return items


def parse_eda_bom(rows):
    """嘉立创 EDA 原理图 BOM：No./Quantity/Comment/Designator/Footprint/Value/
    Manufacturer Part/Manufacturer/Supplier Part/Supplier"""
    items = []
    header = None
    for row in rows[:6]:
        cols = detect_columns(row)
        if cols["qty"] >= 0 and cols["pkg"] >= 0 and cols["spec"] >= 0:
            header = row
            break
    if header is None:
        return items
    cols = detect_columns(header)
    for row in rows[rows.index(header) + 1:]:
        if len(row) <= max(cols.values()):
            continue
        lcsc = sanitize_field(row[cols["lcsc"]] if cols["lcsc"] >= 0 else "", LCSC_MAX)
        if lcsc and not re.match(r"^C\d+$", lcsc, re.IGNORECASE):
            lcsc = ""
        qty = parse_int(row[cols["qty"]])
        name = sanitize_field(row[cols["name"]] if cols["name"] >= 0 else "", NAME_MAX)
        spec = sanitize_field(row[cols["spec"]] if cols["spec"] >= 0 else "", SPEC_MAX)
        pkg = sanitize_field(row[cols["pkg"]] if cols["pkg"] >= 0 else "", PKG_MAX)
        if not (lcsc or name or spec):
            continue
        items.append({"lcsc": lcsc, "name": name, "spec": spec, "pkg": pkg, "qty": qty})
    return items


def parse_standard(rows):
    """标准库存格式：直接用表头检测列。"""
    items = []
    header_idx = None
    for i, row in enumerate(rows[:6]):
        cols = detect_columns(row)
        if cols["qty"] >= 0 and (cols["name"] >= 0 or cols["lcsc"] >= 0):
            header_idx = i
            break
    if header_idx is None:
        return items
    cols = detect_columns(rows[header_idx])
    for row in rows[header_idx + 1:]:
        if len(row) <= max(cols.values()):
            continue
        lcsc = sanitize_field(row[cols["lcsc"]] if cols["lcsc"] >= 0 else "", LCSC_MAX)
        qty = parse_int(row[cols["qty"]])
        name = sanitize_field(row[cols["name"]] if cols["name"] >= 0 else "", NAME_MAX)
        spec = sanitize_field(row[cols["spec"]] if cols["spec"] >= 0 else "", SPEC_MAX)
        pkg = sanitize_field(row[cols["pkg"]] if cols["pkg"] >= 0 else "", PKG_MAX)
        if not (lcsc or name or spec):
            continue
        items.append({"lcsc": lcsc, "name": name, "spec": spec, "pkg": pkg, "qty": qty})
    return items


def parse_inventory_file(raw):
    """Parse an existing INVENTORY.CSV (device standard format) into items."""
    text = decode_bytes(raw)
    reader = csv.reader(io.StringIO(text), delimiter=sniff_delimiter(text))
    rows = list(parse_rows_from_reader(reader))
    return parse_standard(rows) if rows else []


def items_to_csv_bytes(items):
    """Serialize items to device INVENTORY.CSV format (fields must not contain commas)."""
    out = io.StringIO()
    out.write("LCSC,NAME,SPEC,PACKAGE,QTY\n")
    for it in items:
        out.write("{},{},{},{},{}\n".format(
            sanitize_field(it.get("lcsc", ""), LCSC_MAX),
            sanitize_field(it.get("name", ""), NAME_MAX),
            sanitize_field(it.get("spec", ""), SPEC_MAX),
            sanitize_field(it.get("pkg", ""), PKG_MAX),
            int(it.get("qty", 0))))
    return out.getvalue().encode("utf-8")


def merge_inventory(existing, new_items):
    """Merge new items into existing list; same LCSC (or name+spec+pkg) sums qty."""
    merged = list(existing)

    def find_existing(it):
        lcsc = norm_key(it.get("lcsc", ""))
        for i, e in enumerate(merged):
            if lcsc and norm_key(e.get("lcsc", "")) == lcsc:
                return i
        # fallback: name+spec+pkg
        if it.get("name"):
            for i, e in enumerate(merged):
                if (norm_key(e.get("name", "")) == norm_key(it.get("name", "")) and
                        norm_key(e.get("spec", "")) == norm_key(it.get("spec", "")) and
                        norm_key(e.get("pkg", "")) == norm_key(it.get("pkg", ""))):
                    return i
        return -1

    for it in new_items:
        idx = find_existing(it)
        if idx >= 0:
            merged[idx]["qty"] = merged[idx].get("qty", 0) + it.get("qty", 0)
        else:
            merged.append(dict(it))
    return merged


class DeviceLink:
    """Serial line protocol wrapper for the FILE: commands."""

    def __init__(self, port):
        self.ser = serial.Serial(port, BAUD, timeout=0.2, write_timeout=2)
        self.buf = b""

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _read_line(self, timeout=CMD_TIMEOUT):
        """Read one complete line; filters out ESP-IDF log lines."""
        import time
        end = time.time() + timeout
        while time.time() < end:
            # drain whatever is available
            n = self.ser.in_waiting
            if n > 0:
                data = self.ser.read(min(n, RX_CHUNK))
                self.buf += data
            # extract complete lines
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                line = line.rstrip(b"\r").decode("utf-8", "replace").strip()
                if not line:
                    continue
                # skip ESP-IDF logs like "I (123) tag: msg"
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
        """Upload raw bytes to device path. Returns (ok, msg)."""
        self.send(f"FILE:PUT|{path}|{len(data)}")
        line = self._read_line()
        if line != "FILE:READY":
            return False, line or "no ready"
        # send raw bytes, then wait for OK
        self.ser.write(data)
        self.ser.flush()
        resp = self._read_line(CMD_TIMEOUT + len(data) / 115200.0 * 2)
        if resp == "FILE:OK":
            return True, "OK"
        return False, resp or "timeout"

    def _read_raw(self, n, timeout):
        """Read exactly n raw bytes, draining the line-buffer stash first."""
        import time
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
        """Download file from device. Returns (ok, data, msg)."""
        self.send(f"FILE:GET|{path}")
        line = self._read_line()
        if not line.startswith("FILE:READY|"):
            return False, b"", line or "no ready"
        size = int(line.split("|")[1])
        data = self._read_raw(size, CMD_TIMEOUT + size / 115200.0 * 3)
        if len(data) != size:
            return False, data, f"short read {len(data)}/{size}"
        # trailing DONE
        self._read_line(2)
        return True, data, "OK"

    def delete(self, path):
        self.send(f"FILE:DEL|{path}")
        resp = self._read_line()
        return resp == "FILE:OK", resp or "timeout"


class App:
    def __init__(self, root):
        self.root = root
        self.link = None
        root.title("ESP32 库存文件传输工具")
        root.geometry("860x560")
        root.configure(bg="#10141c")

        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TFrame", background="#10141c")
        style.configure("TLabel", background="#10141c", foreground="#e8eef7")
        style.configure("Treeview", background="#1a2434", foreground="#e8eef7",
                        fieldbackground="#1a2434", rowheight=22)
        style.configure("Treeview.Heading", background="#2e86de", foreground="#ffffff")
        style.map("Treeview", background=[("selected", "#1b5fa8")])
        style.configure("TButton", background="#2e86de", foreground="#ffffff",
                        padding=4)
        style.map("TButton", background=[("active", "#1b5fa8")])
        style.configure("TLabelframe", background="#10141c", foreground="#e8eef7")
        style.configure("TLabelframe.Label", background="#10141c", foreground="#e8eef7")

        self._build_connect_bar()
        self._build_body()
        self.status_var = tk.StringVar(value="未连接")
        tk.Label(root, textvariable=self.status_var, bg="#10141c",
                 fg="#00c9a7", anchor="w").pack(fill="x", padx=10, pady=(0, 6))

        # auto refresh ports
        self.refresh_ports()

    # ---------- UI ----------
    def _build_connect_bar(self):
        bar = ttk.Frame(self.root)
        bar.pack(fill="x", padx=10, pady=8)
        ttk.Label(bar, text="串口:").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(bar, textvariable=self.port_var, width=14)
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(bar, text="刷新", command=self.refresh_ports).pack(side="left", padx=4)
        self.conn_btn = ttk.Button(bar, text="连接", command=self.toggle_connect)
        self.conn_btn.pack(side="left", padx=8)
        ttk.Button(bar, text="PING 测试", command=self.do_ping).pack(side="left", padx=4)

    def _build_body(self):
        body = ttk.Frame(self.root)
        body.pack(fill="both", expand=True, padx=10, pady=4)

        # Left: local file picker
        left = ttk.LabelFrame(body, text="PC 本地文件")
        left.pack(side="left", fill="both", expand=True, padx=(0, 5))
        self.local_path_var = tk.StringVar()
        ttk.Entry(left, textvariable=self.local_path_var).pack(fill="x", padx=6, pady=6)
        btns1 = ttk.Frame(left)
        btns1.pack(fill="x", padx=6)
        ttk.Button(btns1, text="选择文件", command=self.pick_local).pack(side="left", padx=2)
        ttk.Button(btns1, text="上传到 /sdcard/", command=self.upload_to_root).pack(side="left", padx=2)
        ttk.Button(btns1, text="导入CSV入库", command=self.import_inventory).pack(side="left", padx=2)
        ttk.Button(btns1, text="CSV出库", command=self.export_inventory).pack(side="left", padx=2)
        btns1b = ttk.Frame(left)
        btns1b.pack(fill="x", padx=6, pady=(4, 0))
        self.local_hint = tk.Label(left, text="入库: 立创订单/标准格式   出库: 原理图BOM",
                                   bg="#10141c", fg="#7c8ca6", anchor="w")
        self.local_hint.pack(fill="x", padx=8)

        self.local_text = tk.Text(left, height=14, bg="#1a2434", fg="#e8eef7",
                                  insertbackground="#e8eef7", wrap="none")
        self.local_text.pack(fill="both", expand=True, padx=6, pady=6)

        # Right: device SD card
        right = ttk.LabelFrame(body, text="设备 SD 卡")
        right.pack(side="right", fill="both", expand=True, padx=(5, 0))
        btns2 = ttk.Frame(right)
        btns2.pack(fill="x", padx=6, pady=4)
        ttk.Button(btns2, text="刷新列表", command=self.refresh_device).pack(side="left", padx=2)
        ttk.Button(btns2, text="下载选中", command=self.download_selected).pack(side="left", padx=2)
        ttk.Button(btns2, text="删除选中", command=self.delete_selected).pack(side="left", padx=2)
        ttk.Button(btns2, text="清空库存", command=self.clear_inventory).pack(side="left", padx=2)

        cols = ("path", "size")
        self.tree = ttk.Treeview(right, columns=cols, show="tree headings", height=14)
        self.tree.heading("#0", text="路径")
        self.tree.heading("size", text="大小")
        self.tree.column("#0", width=300)
        self.tree.column("size", width=70, anchor="e")
        self.tree.pack(fill="both", expand=True, padx=6, pady=6)

        self._local_files = []

    # ---------- helpers ----------
    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def set_status(self, s, ok=True):
        self.status_var.set(s)
        self.root.update_idletasks()

    def log_local(self, msg):
        self.local_text.insert("end", msg + "\n")
        self.local_text.see("end")

    def pick_local(self):
        f = filedialog.askopenfilename(filetypes=[("CSV", "*.csv"), ("所有文件", "*.*")])
        if f:
            self.local_path_var.set(f)
            self.log_local(f"已选择: {f}")

    # ---------- connection ----------
    def toggle_connect(self):
        if self.link:
            self.link.close()
            self.link = None
            self.conn_btn.config(text="连接")
            self.set_status("已断开")
            return
        port = self.port_var.get()
        if not port:
            messagebox.showwarning("提示", "请选择串口")
            return
        try:
            self.link = DeviceLink(port)
            if self.link.ping():
                self.conn_btn.config(text="断开")
                self.set_status(f"已连接 {port} @ {BAUD}，设备就绪")
                self.refresh_device()
            else:
                self.link.close()
                self.link = None
                self.set_status(f"{port} 连接但无响应（确认已烧录新固件）", ok=False)
        except Exception as e:
            self.link = None
            messagebox.showerror("连接失败", str(e))

    def do_ping(self):
        if not self.link:
            return
        self.set_status("PONG ✓" if self.link.ping() else "无响应 ✗", ok=False)

    # ---------- device ops ----------
    def refresh_device(self):
        if not self.link:
            return
        entries, err = self.link.list_dir("/sdcard")
        if err:
            self.set_status(f"列目录失败: {err}", ok=False)
            return
        self.tree.delete(*self.tree.get_children())
        for path, size, kind in entries:
            if path.endswith("/"):
                self.tree.insert("", "end", text=path, values=(path, kind))
            else:
                self.tree.insert("", "end", text=path, values=(path, f"{size} B"))
        self.set_status(f"设备文件: {len(entries)} 项")

    def _selected_path(self):
        sel = self.tree.selection()
        if not sel:
            messagebox.showinfo("提示", "请先在右侧选择一个文件")
            return None
        return self.tree.item(sel[0], "text")

    def download_selected(self):
        if not self.link:
            return
        path = self._selected_path()
        if not path:
            return
        self.set_status(f"下载 {path} ...")
        ok, data, msg = self.link.get(path)
        if not ok:
            self.set_status(f"下载失败: {msg}", ok=False)
            return
        f = filedialog.asksaveasfilename(initialfile=os.path.basename(path),
                                         filetypes=[("CSV", "*.csv"), ("所有文件", "*.*")])
        if f:
            with open(f, "wb") as fh:
                fh.write(data)
            self.log_local(f"已下载 {len(data)} 字节 -> {f}")
            self.set_status(f"下载完成: {len(data)} 字节")

    def delete_selected(self):
        if not self.link:
            return
        path = self._selected_path()
        if not path:
            return
        if not messagebox.askyesno("确认", f"删除设备上的 {path} ?"):
            return
        ok, msg = self.link.delete(path)
        self.set_status("删除成功" if ok else f"删除失败: {msg}", ok=ok)
        self.refresh_device()

    def _upload(self, dest_dir):
        if not self.link:
            return
        f = self.local_path_var.get()
        if not f or not os.path.isfile(f):
            messagebox.showinfo("提示", "请先选择本地文件")
            return
        with open(f, "rb") as fh:
            data = fh.read()
        dev_path = f"{dest_dir}/{os.path.basename(f)}"
        self.set_status(f"上传 {dev_path} ...")
        ok, msg = self.link.put(dev_path, data)
        self.set_status(f"上传{'成功' if ok else '失败'}: {msg}", ok=ok)
        self.log_local(f"上传 {len(data)} 字节 -> {dev_path}")
        self.refresh_device()

    def upload_to_root(self):
        self._upload("/sdcard")

    # ---------- inventory ops ----------

    def _download_inventory(self):
        """Return (ok, items) where items is the current device inventory (may be empty)."""
        ok, data, msg = self.link.get(INV_FILE)
        if not ok:
            # File missing is fine -> empty inventory
            if "cannot open" in msg:
                return True, []
            return False, None
        try:
            return True, parse_inventory_file(data)
        except Exception as e:
            return False, None

    def clear_inventory(self):
        """清空库存：三次确认后上传仅含表头的 INVENTORY.CSV。"""
        if not self.link:
            messagebox.showwarning("提示", "请先连接设备")
            return
        if not messagebox.askyesno("清空库存 (1/3)",
                                   "确定要清空全部库存吗？\n\n将删除 INVENTORY.CSV 中的所有物料记录。"):
            return
        if not messagebox.askyesno("清空库存 (2/3)",
                                   "再确认一次：\n\n清空后设备库存将为 0 条，此操作不可撤销！"):
            return
        if not messagebox.askyesno("清空库存 (3/3)",
                                   "最后一次确认：\n\n真的要把库存清零吗？\n(设备端显示将变为空库存)"):
            return
        self.set_status("正在清空库存 ...")
        header_only = b"LCSC,NAME,SPEC,PACKAGE,QTY\n"
        ok, msg = self.link.put(INV_FILE, header_only)
        if ok:
            self.set_status("库存已清空（仅保留表头）")
            self.log_local("库存已清空 -> /sdcard/INVENTORY.CSV")
        else:
            self.set_status(f"清空失败: {msg}", ok=False)
        self.refresh_device()

    def import_inventory(self):
        """导入 CSV 入库：识别格式 -> 与设备现有库存合并 -> 上传。"""
        if not self.link:
            messagebox.showwarning("提示", "请先连接设备")
            return
        new_items, fmt_name = self._parse_file_items("导入")
        if new_items is None:
            return

        # Download current device inventory
        ok, existing = self._download_inventory()
        if not ok:
            self.set_status("无法读取设备现有库存，已取消", ok=False)
            return
        self.log_local(f"文件格式识别: {fmt_name}，解析到 {len(new_items)} 种物料")

        merged = merge_inventory(existing, new_items)
        preview = [f"{it.get('lcsc','') or '-'} | {it.get('name','')} | "
                   f"{it.get('spec','')} | {it.get('pkg','')} | x{it.get('qty',0)}"
                   for it in new_items[:12]]
        more = "" if len(new_items) <= 12 else f"\n... 共 {len(new_items)} 条"
        summary = (f"识别格式: {fmt_name}\n"
                   f"新增/更新物料: {len(new_items)} 条\n"
                   f"合并后库存: {len(existing)} -> {len(merged)} 条\n\n"
                   + "\n".join(preview) + more +
                   "\n\n确认写入设备 /sdcard/INVENTORY.CSV 吗？")
        if not messagebox.askyesno("导入预览", summary):
            self.set_status("已取消导入")
            return

        data = items_to_csv_bytes(merged)
        self.set_status(f"正在写入库存 ({len(merged)} 条) ...")
        ok, msg = self.link.put(INV_FILE, data)
        if ok:
            self.set_status(f"导入成功：库存 {len(merged)} 条")
            self.log_local(f"已合并入库 {len(new_items)} 条 -> {INV_FILE} ({len(data)} B)")
            self._append_history("IN", new_items)
        else:
            self.set_status(f"导入失败: {msg}", ok=False)
        self.refresh_device()

    def _parse_file_items(self, action):
        """Pick a local CSV, parse it. Returns (items, fmt_name) or (None, None)."""
        f = filedialog.askopenfilename(filetypes=[("CSV", "*.csv"), ("所有文件", "*.*")])
        if not f:
            return None, None
        with open(f, "rb") as fh:
            raw = fh.read()
        text = decode_bytes(raw)
        reader = csv.reader(io.StringIO(text), delimiter=sniff_delimiter(text))
        rows = list(parse_rows_from_reader(reader))
        if not rows:
            messagebox.showwarning("提示", "文件为空或无法解析")
            return None, None

        fmt = detect_format(rows)
        if fmt == "lcsc_order":
            items = parse_lcsc_order(rows)
            fmt_name = "立创商城订单"
        elif fmt == "eda_bom":
            items = parse_eda_bom(rows)
            fmt_name = "嘉立创 EDA 原理图 BOM"
        elif fmt == "standard":
            items = parse_standard(rows)
            fmt_name = "标准库存格式"
        else:
            items = []
            fmt_name = "未知"

        if not items:
            messagebox.showwarning("提示", f"未能从该文件解析出物料（格式：{fmt_name}）\n\n"
                                           "支持：立创商城订单 / 嘉立创 EDA 原理图 BOM / 标准库存格式")
            return None, None
        return items, fmt_name

    def _append_history(self, action, items):
        """Append stock-operation entries to /sdcard/STOCK_HIST.CSV (device file)."""
        import time as _t
        HIST = "/sdcard/STOCK_HIST.CSV"
        ok, data, msg = self.link.get(HIST)
        if ok and data:
            text = data.decode("utf-8", "replace")
            if not text.strip():
                text = "DATETIME,ACTION,SPEC,LCSC,QTY\n"
            elif not text.lstrip().startswith("DATETIME"):
                text = "DATETIME,ACTION,SPEC,LCSC,QTY\n" + text
        else:
            text = "DATETIME,ACTION,SPEC,LCSC,QTY\n"
        stamp = _t.strftime("%Y-%m-%d %H:%M:%S")
        for it in items:
            spec = (it.get("spec") or "").replace(",", " ").replace("\n", " ")
            lcsc = (it.get("lcsc") or "").replace(",", " ").replace("\n", " ")
            text += f"{stamp},{action},{spec},{lcsc},{int(it.get('qty',0))}\n"
        self.link.put(HIST, text.encode("utf-8"))
        self.log_local(f"已记录 {len(items)} 条{action}历史 -> {HIST}")

    def export_inventory(self):
        """CSV 出库：识别格式（通常是原理图 BOM）-> 从设备库存扣减 -> 上传。"""
        if not self.link:
            messagebox.showwarning("提示", "请先连接设备")
            return
        items, fmt_name = self._parse_file_items("出库")
        if items is None:
            return

        ok, existing = self._download_inventory()
        if not ok:
            self.set_status("无法读取设备现有库存，已取消", ok=False)
            return
        self.log_local(f"文件格式识别: {fmt_name}，解析到 {len(items)} 条出库物料")

        # Subtract quantities; track what would go negative / missing.
        after = [dict(e) for e in existing]
        missing = []
        negative = []
        for it in items:
            idx = -1
            lcsc = norm_key(it.get("lcsc", ""))
            for i, e in enumerate(after):
                if lcsc and norm_key(e.get("lcsc", "")) == lcsc:
                    idx = i
                    break
            if idx < 0 and it.get("name"):
                for i, e in enumerate(after):
                    if (norm_key(e.get("name", "")) == norm_key(it.get("name", "")) and
                            norm_key(e.get("spec", "")) == norm_key(it.get("spec", "")) and
                            norm_key(e.get("pkg", "")) == norm_key(it.get("pkg", ""))):
                        idx = i
                        break
            if idx < 0:
                missing.append(it)
                continue
            after[idx]["qty"] = after[idx].get("qty", 0) - it.get("qty", 0)
            if after[idx]["qty"] < 0:
                negative.append((after[idx]["name"] or after[idx]["lcsc"], after[idx]["qty"]))

        preview = [f"{it.get('lcsc','') or '-'} | {it.get('name','')} | "
                   f"{it.get('spec','')} | x{it.get('qty',0)}"
                   for it in items[:10]]
        more = "" if len(items) <= 10 else f"\n... 共 {len(items)} 条"
        miss = f"\n⚠ 未在库存中找到: {len(missing)} 条" if missing else ""
        neg = f"\n⚠ 出库后将为负数: {len(negative)} 条" if negative else ""
        summary = (f"识别格式: {fmt_name}\n"
                   f"出库物料: {len(items)} 条\n"
                   f"出库后库存: {len(existing)} -> {len(after)} 条\n\n"
                   + "\n".join(preview) + more + miss + neg +
                   "\n\n确认从设备库存扣减吗？")
        if not messagebox.askyesno("出库预览", summary):
            self.set_status("已取消出库")
            return

        data = items_to_csv_bytes(after)
        self.set_status(f"正在写入库存 ({len(after)} 条) ...")
        ok, msg = self.link.put(INV_FILE, data)
        if ok:
            self.set_status(f"出库成功：库存 {len(after)} 条")
            self.log_local(f"已扣减 {len(items)} 条 -> {INV_FILE} ({len(data)} B)")
            self._append_history("OUT", items)
        else:
            self.set_status(f"出库失败: {msg}", ok=False)
        self.refresh_device()


def main():
    if serial is None:
        print("缺少 pyserial，请先安装：pip install pyserial")
        raise SystemExit(1)
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
