# UTF-8 编码规范

本工程源码、脚本、CSV、Markdown、TXT 统一保存为 UTF-8。

- C/C++：CMake 强制 `-finput-charset=UTF-8 -fexec-charset=UTF-8`。
- Windows：`build_flash.bat` 自动切换代码页 65001，并设置 Python UTF-8 环境。
- Python：读取外部 CSV 时优先 UTF-8/UTF-8 BOM，必要时兼容 GB18030；写回工程/设备时统一 UTF-8。
- INVENTORY：固定使用 UTF-8，列为 `PRODUCT_NO,MODEL,QTY`。
- Git/编辑器：`.editorconfig` 与 `.gitattributes` 固定文本换行和 UTF-8 约定。

注意：串口终端本身也必须选择 UTF-8；否则固件发送的 UTF-8 中文仍可能在终端中显示为乱码。
