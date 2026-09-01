# SD 卡问题诊断与本次修复

监控日志的关键顺序是：

1. 固件启动并初始化 LVGL / Wi-Fi；
2. SD 流程首次进入根目录访问，但 `opendir("/sdcard")` 失败；
3. 随后多次重新挂载都返回 `ESP_ERR_NO_MEM`；
4. `CMD5/CMD52 command not supported` 属于 SDSPI 探测过程中的兼容性响应，本次不把它作为主故障。

本版修改：
- 在 Wi-Fi / LVGL 大量占用内部 RAM 之前优先尝试挂载 SD；
- SD 扫描任务栈 8192 -> 4096 bytes；
- FATFS max_files 12 -> 6；
- `opendir()` 因 ENOMEM 失败时保持已挂载卷，不再立即卸载/重挂载；
- 非 ENOMEM 根目录错误连续两次后才执行干净卸载；
- SD 页面直接显示 `MEMORY BUSY - RETRY`、`CHECK CARD / FAT32` 和具体 `esp_err_to_name()`；
- 串口日志增加剩余 heap、errno 和连续失败次数。

如果刷入本版后仍无法读取，请优先检查：
- SD 卡为 FAT32；
- 3.3 V 供电稳定；
- CS=GPIO5、SCK=18、MISO=19、MOSI=23 与板卡一致；
- 卡座/杜邦线接触可靠且线不要过长；
- 用另一张已知正常的 SD 卡交叉验证。
