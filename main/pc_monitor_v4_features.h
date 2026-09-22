#ifndef PC_MONITOR_V4_FEATURES_H
#define PC_MONITOR_V4_FEATURES_H

/* PC Monitor v4.0 Dashboard feature definitions
 *
 * Gauge:
 *  - CPU usage gauge
 *  - GPU usage gauge
 *  - Memory usage gauge
 *
 * Alarm thresholds:
 *  - CPU/GPU temperature normal: < 70C
 *  - Warning: 70C ~ 85C
 *  - Critical: > 85C
 *
 * Network telemetry fields reserved:
 *  NET_RX_KBPS
 *  NET_TX_KBPS
 */

#define PCMON_V4_REFRESH_MS 1000
#define PCMON_TEMP_WARNING_C 70
#define PCMON_TEMP_CRITICAL_C 85

#endif
