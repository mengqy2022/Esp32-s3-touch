# 2026-09-01 工程更新

## SD 卡
- 启动早期先挂载 SD，避开 Wi-Fi/LVGL 占用大量内部堆后再挂载。
- SD 扫描任务栈从 8192 降到 4096 bytes。
- FATFS `max_files` 从 12 降到 6。
- `opendir()` 若因 ENOMEM 失败，不再立刻卸载，避免反复 mount/unmount 后持续 `ESP_ERR_NO_MEM`。
- 日志增加 errno、剩余 heap、失败连续次数，便于继续定位硬件/文件系统问题。

## INVENTORY
- INVENTORY 固定精简为 `PRODUCT_NO,MODEL,QTY`。
- 已将提供的 4 份立创商城 CSV 合并：187 条采购明细 -> 169 种物料，累计订购 7085 件。
- 同一商品编号自动累计数量。
- 固件加载器兼容旧 `LCSC,NAME,SPEC,PACKAGE,QTY` 文件；保存时自动转成新三列格式。
- CSV/BOM 解析支持表头前存在订单金额等摘要行。

## UTF-8
- 工程源码/脚本/CSV/TXT/Markdown 统一 UTF-8。
- Windows 构建脚本自动 `chcp 65001`，并启用 Python UTF-8。
- 编译器显式设置 UTF-8 输入/执行字符集。
- 修复原 ZIP 中已经乱码的中文文件名，统一改成稳定的 ASCII 文件名（内容仍为 UTF-8 中文）。

## UX
- INVENTORY 页面改成 MODEL / QTY / PART# 三列，搜索也只聚焦型号和商品编号。
- 保留库存过滤、历史、数量 +/- 和删除功能；数据更干净，查找更直接。
