# ESP32 Touch Toolbox + Vocab Lab

面向 **ESP32-WROOM-32E + 2.8" ILI9341 320×240 + XPT2046 电阻触摸 + MicroSD** 的离线触屏工具系统。

本版本在保留原有 SD 检测、Wi‑Fi、系统信息、数独、库存/BOM、串口文件传输等功能的基础上，重新整理了 GUI 与 UI 调度，并新增一个面向触摸屏的 **Vocab Lab 单词学习模块**：本地词书、Wi‑Fi 下载词书、每日计划、学习卡、间隔复习、触屏默写、进度持久化。

> Vocab Lab 只参考常见背词产品的交互思路。没有复制百词斩的代码、UI 素材、图片、商业词库或付费功能。

## 功能一览

### 原有功能（保留）

- SD 卡挂载、容量/目录/实际读取检测
- Wi‑Fi 扫描、连接、保存凭据与自动重连
- SNTP 时间与联网定位信息
- 触摸 3 点校准并保存到 NVS
- 数独小游戏
- 电子元器件库存 / BOM 入库出库 / 历史记录
- USB 串口文件传输工具
- RGB 状态指示灯

### 新增 Vocab Lab

- **离线优先**：词书与学习进度放在 MicroSD，不把整本词书加载进 RAM
- **固定目录**：`/sdcard/vocabulary/books/`
- **在线词书下载**：连接 Wi‑Fi 后可在设备上直接下载并自动转换/安装
- **本地词书选择**：支持标准 UTF‑8 CSV
- **每日计划**：5~100 词/天，设置保存在 NVS
- **学习模式**：单词 / 音标 / 中文释义，`AGAIN / SHOW / KNOWN`
- **默写模式**：中文释义提示 + 触屏软键盘输入英文拼写
- **轻量 SRS**：优先到期复习，再加入新词；不同熟练度安排不同复习间隔
- **进度持久化**：每本词书独立 `.vcp` 文件；正确数、错误数、已学习数、今日完成数均保留
- **断电友好**：学习进度每次作答后即时写入 SD

## UI / 流畅度改造

- 统一为暗色简约、高对比度触屏风格
- 标题区与返回按钮使用独立安全区域，避免文字与按钮互相遮盖
- 主菜单改为双列可滚动模块区 + 固定状态卡
- Wi‑Fi 列表与密码键盘拆分成两个视图，键盘不再盖住 AP 列表
- Vocab Lab 页面针对 320×240 单独布局，关键触控按钮保持较大点击面积
- LVGL 绘制缓冲从 24 行增加到 32 行
- UI 命令队列从 8 增加到 24，降低快速触摸/状态刷新时的命令丢失概率
- SD 检测改为**保持健康挂载**，不再每 3 秒反复卸载/挂载；避免文件句柄失效和明显卡顿
- SD SPI 由原先较保守的 4 MHz 提升到 8 MHz
- HTTPS 词书下载使用 ESP-IDF CA bundle 校验证书

详细设计见：[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) 与 [`docs/VOCABULARY.md`](docs/VOCABULARY.md)。

## 硬件引脚

| 外设 | 信号 | GPIO |
|---|---|---:|
| ILI9341 | SCK / MOSI / MISO / CS / DC / BL | 14 / 13 / 12 / 15 / 2 / 21 |
| XPT2046 | SCK / MOSI / MISO / CS / IRQ | 25 / 32 / 39 / 33 / 36 |
| MicroSD | SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |
| RGB LED | R / G / B | 17 / 22 / 16 |

完整定义在 `main/board_pins.h`。

## 软件环境

推荐使用：

- ESP-IDF **v5.5.4**（GitHub Actions 也以该版本构建）
- ESP32 target
- LVGL **8.4.x**（由 ESP-IDF Component Manager 获取）
- FAT32 MicroSD

工程不提交 `managed_components/`、`sdkconfig` 与 `build/`，首次构建会自动恢复依赖。

本整理包没有保留改动前生成的 `dependencies.lock`（其 manifest 哈希已失效）。首次在 ESP-IDF v5.5.4 下成功执行 `idf.py build` 后会生成新的 lock 文件，建议将那份有效的 `dependencies.lock` 一并提交到 GitHub，以获得更稳定的依赖复现。

## 构建与烧录

### Linux / macOS

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

也可以：

```bash
./build_flash.sh /dev/ttyUSB0
```

### Windows

在 ESP-IDF PowerShell 中：

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

或：

```bat
build_flash.bat COM5
```

## MicroSD 初始化

最简单的方法：把仓库中的 `sdcard_template/` 内容复制到 SD 卡根目录。

```text
/sdcard/
├── INVENTORY.CSV
└── vocabulary/
    ├── books/
    │   └── starter.csv
    ├── progress/
    └── cache/
```

设备正常挂载 SD 后，进入：

`VOCAB LAB -> BOOKS -> starter`

即可离线开始学习和默写。

## 词书格式

设备格式为 UTF‑8 CSV：

```csv
word,phonetic,meaning,example
ability,/əˈbɪləti/,能力；才能,Practice improves your ability.
```

其中：

- 必填：`word`, `meaning`
- 可选：`phonetic`, `example`
- 支持带引号的 CSV 字段以及字段内部逗号
- 单行建议不要超过 1 KB

PC 上也可以转换自己的 CSV / JSON：

```bash
python tools/vocab_convert.py my_words.json sdcard_template/vocabulary/books/my_words.csv
```

详见 [`docs/VOCABULARY.md`](docs/VOCABULARY.md)。

## 内置在线词书目录

设备提供以下远程入口：

- CET‑4 Core / High Frequency
- CET‑6 Core / High Frequency
- 考研 Core / High Frequency
- TOEFL Core / High Frequency
- IELTS Basic / Core / Advanced
- GRE / GMAT / SAT

这些下载入口指向公开的、由 ECDICT 客观派生的 MIT 许可词库。设备下载 JSON 后以流式方式转成轻量 CSV，并将最终文件放到 `/sdcard/vocabulary/books/`；下载缓存放到 `/sdcard/vocabulary/cache/` 并在安装完成后清理。

来源与许可说明见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。

## 工程结构

```text
.
├── .github/workflows/build.yml   # GitHub Actions ESP-IDF 构建
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── README.md
├── README_CN.md
├── THIRD_PARTY_NOTICES.md
├── docs/
│   ├── ARCHITECTURE.md
│   ├── VOCABULARY.md
│   └── CHANGELOG.md
├── main/
│   ├── main.c
│   ├── ui.c / ui.h
│   ├── lv_port.c / lv_port.h
│   ├── vocabulary.c / vocabulary.h
│   ├── sd_monitor.c / sd_monitor.h
│   ├── wifi_mgr.c / wifi_mgr.h
│   ├── inventory.c / inventory.h
│   ├── file_xfer.c / file_xfer.h
│   ├── sudoku.c / sudoku.h
│   ├── net_utils.c / net_utils.h
│   ├── lcd_ili9341.c / .h
│   ├── xpt2046.c / .h
│   └── board_pins.h
├── sdcard_template/
└── tools/
    ├── inventory_tool.py
    ├── vocab_convert.py
    └── smoke_test.py
```

## 重要实现说明

### 为什么 SD 不再周期性卸载？

库存、串口文件传输、词书和学习进度都会长期访问 `/sdcard`。周期性卸载会让这些模块正在使用的 `FILE*` 失效，也会让 UI 感觉卡顿。现在 `sd_monitor_probe()` 会复用健康挂载；只有检测到根目录访问失败时，才丢弃旧挂载并在下一次重新挂载。

### 为什么词书不整体载入内存？

ESP32-WROOM-32E RAM 有限，而考试词书可能包含数千词。Vocab Lab 只在创建学习会话时保存少量“词索引 + 文件偏移”，每次展示当前单词时再从 SD 读取对应行，因此大词书也不会显著增加常驻 RAM。

### 为什么没有加入百词斩图片联想/商业内容？

本工程目标是可公开上传、可离线运行的嵌入式实现。第三方商业图片、课程、会员服务和商业词书不适合直接复制。当前实现保留了对学习体验最关键、也适合这块硬件的部分：词书、计划、学习、复习、进度、拼写/默写与离线存储。

## 已知限制

- 当前硬件定义中没有扬声器/I2S 音频输出，因此没有做单词发音播放；后续若增加 MAX98357A / I2S DAC 可扩展音频模块。
- 远程 ECDICT 派生词库主要提供单词、音标、中文释义；例句字段可能为空。
- 320×240 屏幕空间有限，Vocab Lab 特意优先保证大触控区，而不是堆叠大量信息。
- 若系统时间尚未通过 SNTP 校准，SRS 会仍可学习/复习，但“按自然日重置今日完成数”的精确性依赖有效系统时间。

## GitHub CI

仓库包含 `.github/workflows/build.yml`，push / pull request 时用官方 Espressif ESP-IDF CI Action 构建 ESP32 target。这样能尽早发现缺失依赖和编译回归。

## License

本仓库没有替你擅自选择项目整体开源许可证。公开 GitHub 仓库前，请根据你的发布方式添加合适的 `LICENSE`。

第三方组件、词库和现有嵌入式字形数据的许可/来源请分别核对 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
