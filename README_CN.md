# 中文说明

主说明已经合并到仓库根目录 [`README.md`](README.md)，包含硬件、构建、GUI 改造、Vocab Lab、SD 目录、词书格式和 GitHub CI。

如果你从旧版工程升级，请重点阅读：

- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — 本次修改清单
- [`docs/VOCABULARY.md`](docs/VOCABULARY.md) — 单词学习 / 默写 / 下载词库
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — 线程、LVGL、SD 与模块关系

## 本版本重点

- SD：启动早期挂载 + 避免 ENOMEM 时反复卸载/重挂载。
- INVENTORY：精简为 `PRODUCT_NO,MODEL,QTY`，模板已导入本次 4 份物料 CSV。
- UTF-8：源码、CSV、脚本和 Windows 串口构建环境统一 UTF-8。
- 详细变更见 [`RELEASE_NOTES_2026-09-01.md`](RELEASE_NOTES_2026-09-01.md)。
