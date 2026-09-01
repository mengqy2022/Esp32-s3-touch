# 本次修改说明

已针对你提供的编译 LOG 和工程完成以下修改：

1. **修复编译报错**
   - 修复 `main/vocabulary.c` 中 `dirent.d_name -> filename[48]` 的 `-Werror=format-truncation`。
   - 没有关闭 `-Werror`，而是从代码层面做有界复制。

2. **INVENTORY 大数据量体验优化**
   - `INV_MAX_ITEMS` 从 100 提升至 256。
   - 删除原先“最多渲染 24 行”的硬限制。
   - 改为 **8 行控件池 + 虚拟滚动**：库存再多也只维护少量 LVGL 控件，滚动时复用。
   - ALL / IN / OUT / HIST、搜索、选中行、数量修改都尽量局部刷新。
   - 搜索范围扩展为 NAME / SPEC / LCSC。
   - HISTORY 改为保留最近记录。
   - CSV 解析改为单遍解析，并给库存文件读写增加 stdio 缓冲。

3. **系统整体流畅度优化**
   - 修复原先全局 `350ms` 点击节流造成的“按了没反应/连续操作卡顿”问题。
   - 现在：切屏保护 180ms，正常点击防抖 80ms。
   - 周期刷新文本未变化时不重复触发 LVGL 文本重绘。
   - LVGL 行缓冲 32 -> 40 行，并标记为 DMA 内存。
   - LCD SPI 20MHz -> 40MHz。
   - LVGL flush 从大量 512B SPI 小事务改成单次大 DMA 事务。
   - 去掉启动和触摸重新校准后的重复整屏绘制。

## 你本机建议执行

```bash
idf.py fullclean
idf.py build
```

由于当前执行环境没有 ESP-IDF Xtensa 交叉编译工具链，我无法在这里完成 ESP32 目标固件的最终 `idf.py build`；但已对本次关键 C 修改做 GCC 警告/语法检查，并运行了仓库内 CSV 解析回归测试。

> 如果 40MHz LCD SPI 在你的具体硬件上出现花屏/偶发显示异常，把
> `main/lcd_ili9341.c` 中 `LCD_SPI_CLOCK_HZ` 改成 30MHz 或 20MHz 即可；其它优化不受影响。

## V3：VOCAB LAB / BOOKS 下载提示增强

- 点击 `[DOWNLOAD]` 后，顶部状态框会立即显示下载状态。
- 下载过程中显示：词书名称、百分比、已下载 KB / 总 KB。
- 下载完成后持续显示绿色提示：
  `DOWNLOAD COMPLETE!`
  `SAVED TO SD: xxx.csv | Installed xxxx words`
- 对应词书按钮会变成 `[ON SD]`，表示已经真实安装到 SD 卡。
- 下载失败会持续显示红色 `DOWNLOAD FAILED!` 和具体失败原因。
- 成功/失败提示不会瞬间消失，会保留到你开始下一次下载或离开页面。

## V4：修复 BOOKS 下载提示编译错误

- 修复 `ui.c` 中 `refresh_vocab_download_label()` 在首次调用前缺少前向声明的问题。
- 保留 V3 的下载进度、下载完成、保存到 SD、失败原因、`[ON SD]` 等提示功能。
- 本次错误对应 GCC：
  `implicit declaration of function 'refresh_vocab_download_label'`
  和
  `static declaration ... follows non-static declaration`

## V4：BOOKS 下载 / 本地选择 / 布局修复

- 修复 LOG 中 `refresh_vocab_download_label()` 使用前未声明导致的 `-Werror` 编译失败。
- 在线下载改为双源：优先 jsDelivr CDN，失败自动回退 GitHub Raw。
- 下载 JSON 转 CSV 改成“逐单词对象流式解析”，兼容单行/多行 JSON，不把整本词书加载到 RAM。
- 下载成功后自动将新词书设为 ACTIVE。
- 本地 CSV 支持 UTF-8 BOM；Windows/Excel 保存的 UTF-8 CSV 不会再出现“能识别文件但无法选择”的情况。
- 点击本地词书：直接 SELECT -> 启动 LEARN；失败会在顶部明确显示原因。
- ONLINE 已下载词书 `[ON SD]` 再点击时直接打开 LEARN，不重复下载。
- WORDBOOKS 顶部状态框改成固定 38px 两行短文本，列表从 y=81 开始，避免 `ONLINE BOOKS ... appear above` 文字越界/重叠。
- 本地列表标题明确为 `LOCAL BOOKS - TAP TO LEARN`，已选词书显示 `[ACTIVE]`。


## V5：SD 卡 JSON 词库直导 + 编译修复

- 修复 `vocabulary.c` 下载失败提示触发的 `-Werror=format-truncation`。
- 修复 JSON -> CSV 流式转换把 `sizeof(obj)`（指针大小）误当成 4096 字节缓冲区的关键问题。
- `WORDBOOKS` 的 SD BOOKS 现在同时识别 `.csv` 和兼容的 `.json`。
- 手工下载的 WordTyper JSON 可直接复制到 `/vocabulary/books/`；点击后固件本地转换为同名 CSV、设为 ACTIVE，并立即进入 LEARN。
- 如果同名 CSV 已存在，则列表优先显示 CSV，避免 JSON/CSV 重复条目。
- 转换大词库时每 64 个词主动 yield 一次，降低 UI 任务长时间占用 CPU 的风险。
- 新增 `词库手工下载说明.txt`，内含 CET4/CET6/考研/TOEFL/IELTS/GRE/GMAT/SAT 的 CDN + GitHub Raw 地址。

## V6：词库加载体验 / UTF-8 / 手写听写 / 每日计划

### 1. JSON 首次导入不再“假死”
- 点击 SD 卡 `.json` 词库后，JSON -> CSV、CSV 校验、进度文件准备、当天学习队列构建全部放到后台 FreeRTOS task。
- WORDBOOKS 顶部会显示 `SD JSON IMPORT xx%`、已转换单词数，以及后续 `CHECKING / PREPARING` 状态。
- LVGL 线程不再承担整本词库转换，触摸界面在导入期间仍可刷新。
- 大词库扫描/计划统计改成顺序批量读取进度记录，去掉数千次 FAT 随机 `fseek`，降低卡顿。

### 2. 中文/音标乱码保护
- CSV/JSON 进入学习结构前使用 UTF-8 安全截断，绝不在中文多字节字符中间截断。
- 中文释义和例句显示前逐 Unicode 字符检查 `font_cn16` 是否包含字形；缺失字形显示 `?`，不再输出损坏 UTF-8/乱码。
- 当前内置字体不含完整 IPA，因此 LEARN 音标采用 ASCII 兼容显示（例如 `ə -> @`、`ʃ -> sh`、`θ -> th`、`ŋ -> ng`、长音 `ː -> :`），优先保证可读且不乱码。

### 3. DICTATION 增加触屏手写区域
- 默认进入 `HAND` 输入方式，提供 216x82 手写板。
- 每次书写一个清晰的大写英文字母，点 `ADD` 后设备本地识别 A-Z，并以小写追加到答案输入框。
- 提供 `CLEAR`、`DEL`，以及 `KEY/HAND` 切换；键盘输入仍保留。
- 识别器使用轻量 5x7 形状模板，不依赖网络，适合 ESP32；它不是手机级连续英文手写 OCR，建议逐个大写印刷体字母输入。

### 4. 每日计划真正按“当天唯一单词”记忆
- 进度文件升级为 V2，每个单词增加 `last_study_day`。
- LEARN 中同一个单词当天只计入每日任务一次；当天再次进入 LEARN 会继续“剩余任务”，不会重新发完整日计划。
- 每日队列：先到期复习，再用新词填满剩余每日目标；当天已经完成过的单词不会重复占当天名额。
- DICTATION 只从已经学过的单词中出题，不再偷偷消耗新词每日任务。
- 旧 V1 进度会自动迁移，累计 learned/correct/wrong/SRS 保留。由于旧格式没有逐词日期标记，升级当天的 `today_done` 会重置一次，之后按 V2 精确记录。

### 5. 日期/网络策略
- 启动时设置本项目默认时区为 China Standard Time (UTC+8)。如设备长期在其他时区使用，请修改 `main/net_utils.c` 的 `NET_DEFAULT_TZ`。
- 获取 SNTP 后，将可信“本地日期”写入 NVS；以后冷启动没网时至少不会错误回到 1970 或误重置进度。
- **不强制联网才能打开 VOCAB LAB**：词库和学习记录都可以离线使用。
- 但设备没有独立 RTC 时，关机期间无法知道是否跨过午夜。因此首次使用、以及关机跨天后，为了保证“明日计划”准确，建议先连 Wi-Fi 等待 `DATE OK / DATE SYNCED` 后再开始当天 LEARN。
- `DATE CACHED` 表示有上一次可信日期，可以离线学习，但若设备关机跨天，应联网刷新；`TIME NEEDED` 表示从未得到可信日期。
