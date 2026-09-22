
<h1 align="center">ESP32 Touch Toolbox</h1>
<p align="center">
  <b>单词实验室 · 电脑监控仪表盘 · SD 检测 · 元器件库存 · 小游戏</b><br/>
  面向 ESP32 + 2.8" ILI9341 彩色电阻触摸屏的专业化离线固件
</p>

<p align="center">
  <a href="https://www.espressif.com/en/products/socs/esp32"><img src="https://img.shields.io/badge/ESP--IDF-v5.5.4-3B82F6?logo=espressif&logoColor=white" alt="ESP-IDF v5.5.4"/></a>
  <a href="#"><img src="https://img.shields.io/badge/LVGL-8.4-8E6CE0" alt="LVGL 8.4"/></a>
  <a href="#"><img src="https://img.shields.io/badge/board-ESP32--WROOM--32E-16A34A" alt="ESP32-WROOM-32E"/></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-22C55E" alt="License: MIT"/></a>
</p>

<!-- 上传后请把上方 CI 徽章里的 <owner>/<repo> 替换成你的 GitHub 仓库名。 -->

---

🌏 README in
[**英文**](https://github.com/mengqy2022/Esp32-s3-touch) 

## 项目简介

**ESP32 Touch Toolbox** 是一套面向经典组合
"ESP32-WROOM-32E + ILI9341 2.8 寸 320×240 电阻触摸屏 + MicroSD"的完整固件，
把日常工具、学习软件和娱乐整合到一台设备上：

- **Vocab Lab 单词实验室** —— 离线背单词：本地词书、每日计划、间隔复习、触屏默写。
- **PC Monitor 电脑监控屏** —— 大字时钟 + USB 时间同步 + PC 的 CPU/GPU/MEM
  使用率与温度实时仪表盘。
- **经典工具** —— SD 卡检测、Wi-Fi 管理、系统信息、电子元器件库存出入库、
  串口文件传输。

配置完成后完全离线运行：无云、无账号、无订阅。

> Vocab Lab 仅参考常见背词软件的交互思路，不包含任何商业图片、课程、
> 付费词库或会员服务，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 功能亮点

| | |
|---|---|
| 🖥️ **PC Monitor 屏幕** | 大字 HH:MM:SS 时钟、日期/时区/时间来源行、主机连接状态、CPU/GPU/MEM 进度条（≥70% 变黄、≥90% 变红）与温度。 |
| ⏱️ **Type-C 时间同步** | 通过串口每 15 秒校准一次时钟（`PC:TIME`），时区偏移自动下发并持久化到 NVS —— 没有 Wi-Fi 也能准。 |
| 💤 **待机时钟** | 主菜单闲置 2 分钟自动切回时钟仪表盘，点击屏幕任意位置返回。 |
| 🛡️ **校准确认** | 菜单里的 `CALIBRATE` 现在先弹确认对话框，杜绝误触清空校准数据。 |
| 📖 **离线优先词书** | 词书保存在 MicroSD 上并按行流式读取，几千词的大词书也不占内存。 |
| 🔄 **串口优雅共存** | `FILE:` 文件传输与 `PC:` 遥测共用同一控制台串口，由单一读任务按前缀分发。 |

## 硬件

目标硬件组合：**ESP32-WROOM-32E · 2.8" ILI9341 SPI 屏（320×240）·
XPT2046 电阻触摸 · MicroSD（SPI）· 共阳 RGB LED**。

| 外设 | GPIO |
|---|---|
| ILI9341（SCK/MOSI/MISO/CS/DC/BL） | 14 / 13 / 12 / 15 / 2 / 21 |
| XPT2046（SCK/MOSI/MISO/CS/IRQ） | 25 / 32 / 39 / 33 / 36 |
| MicroSD（SCK/MISO/MOSI/CS） | 18 / 19 / 23 / 5 |
| RGB LED（R/G/B，低电平点亮） | 17 / 22 / 16 |
| BOOT 按键（长按重新校准） | GPIO0 |

完整引脚图、接线说明与元件 BOM 集中在一份文件里：
**[docs/HARDWARE.md](docs/HARDWARE.md)**。

## PC Monitor 快速上手

屏幕由设备渲染，PC 只负责喂数据：

```bash
pip install pyserial
python tools/pc_monitor_host.py            # 自动识别串口
python tools/pc_monitor_host.py --port COM8
```

- Windows：CPU/内存走 `GetSystemTimes`/`GlobalMemoryStatusEx`（ctypes，
  零依赖），GPU 走 NVIDIA NVML，CPU 温度走 WMI（尽力而为）。
- Linux：`/proc` 读取 CPU/内存，`nvidia-smi` 读取 GPU。
- 时间与时区自动下发，设备将时区写入 NVS，断电不丢。

协议细节见 [docs/PROTOCOL.md](docs/PROTOCOL.md)，中文手册见
[docs/PC_MONITOR_CN.md](docs/PC_MONITOR_CN.md)。

## 构建与烧录

ESP-IDF **v5.5.4**，target `esp32`，LVGL 8.4（Component Manager 自动拉取）。

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor     # Windows 用 COMx
```

快捷脚本：Linux/macOS 用 `./build_flash.sh <端口>`，Windows 用
`build_flash.bat <端口>`。`build/`、`sdkconfig`、`managed_components/`
均不入库，首次构建自动恢复依赖。

> 从 PC Monitor 合并之前的旧构建升级时，先执行一次 `idf.py fullclean`，
> 让 `sdkconfig.defaults` 新启用的大字时钟字体 `Montserrat 28` 生效。

## MicroSD 初始化

把 `sdcard_template/` 整个复制到一张 FAT32 格式的 MicroSD 卡根目录：

```text
/sdcard/
├── INVENTORY.CSV           库存：PRODUCT_NO,MODEL,QTY（UTF-8）
├── BOM_IN/  BOM_OUT/       入库/出库 CSV 交换目录
└── vocabulary/
    ├── books/              词书 CSV（UTF-8）
    ├── progress/           每本词书的学习进度
    └── cache/              下载临时目录
```

设备挂载 SD 卡后进入：**VOCAB LAB → BOOKS → starter** 即可离线学习；
或连上 Wi-Fi 直接在设备上下载 CET / TOEFL / IELTS 词书。

## 仓库结构

```text
.
├── .github/workflows/build.yml    # ESP-IDF v5.5.4 CI，上传固件产物
├── main/                          # 固件源码（ESP-IDF 组件）
│   ├── main.c                     # 启动、任务、待机时钟策略
│   ├── ui.c / ui.h                # LVGL 屏幕、主题、确认对话框
│   ├── pc_monitor.c / .h          # PC: 遥测协议与状态
│   ├── vocabulary.c / .h          # Vocab Lab 引擎 + 词书下载
│   ├── inventory.c / .h           # 元器件库存/BOM
│   ├── file_xfer.c / .h           # 控制台串口 FILE: 协议
│   ├── net_utils.c / .h           # SNTP、定位、USB 时间同步、时区
│   ├── wifi_mgr.c / .h            # Wi-Fi 扫描连接 + 凭据 NVS
│   ├── sd_monitor.c / .h          # SD 生命周期管理者
│   ├── lcd_ili9341.c / .h         # 裸机 LCD 驱动
│   ├── xpt2046.c / .h             # 触摸驱动 + 三点校准
│   ├── sudoku.c · game2048.c · flappy.c
│   ├── lv_port.c / .h             # LVGL 任务、命令队列、触摸时间戳
│   └── board_pins.h               # GPIO 唯一定义处
├── tools/
│   ├── pc_monitor_host.py         # PC Monitor 主机端（Windows/Linux）
│   ├── inventory_tool.py          # 图形化串口库存管理
│   ├── vocab_convert.py           # JSON/CSV → 设备词书
│   ├── material_to_inventory.py   # 立创订单 CSV → INVENTORY.CSV
│   └── smoke_test.py / test_parse.py
├── docs/
│   ├── HARDWARE.md                # 引脚、接线、BOM
│   ├── PROTOCOL.md                # FILE: + PC: 串口协议
│   ├── ARCHITECTURE.md            # 任务模型与模块职责
│   ├── VOCABULARY.md              # 词书格式、计划、SRS、默写
│   ├── PC_MONITOR_CN.md           # PC Monitor 中文手册
│   └── images/                    # logo 与截图槽位
└── sdcard_template/               # SD 卡初始内容
```

## 文档索引

| 文档 | 内容 |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 任务模型、线程规则、UI 调度、SD 归属 |
| [docs/HARDWARE.md](docs/HARDWARE.md) | 引脚表、接线注意、元件 BOM |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | 控制台串口协议（`FILE:` 与 `PC:`） |
| [docs/VOCABULARY.md](docs/VOCABULARY.md) | 词书格式、每日计划、SRS、默写 |
| [README.md](README.md) | English documentation |
| [CHANGELOG.md](CHANGELOG.md) | 版本历史 |

## 贡献与许可

欢迎贡献，规则见 [CONTRIBUTING.md](CONTRIBUTING.md)（离线优先、内存友好、
不打包商业素材）。

本项目采用 **MIT 许可证** —— 见 [LICENSE](LICENSE)。第三方组件与词书来源
的许可说明集中在 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，
公开发布或商业分发前请务必核对。

---

*在 ESP32-WROOM-32E + ILI9341 320×240 + XPT2046 上设计并真机验证。*