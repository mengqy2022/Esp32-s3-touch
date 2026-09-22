#ifndef PC_MONITOR_V5_AIDA64_FEATURES_H
#define PC_MONITOR_V5_AIDA64_FEATURES_H

/*
 PC Monitor v5.0 AIDA64 Style Dashboard

 Features:
 - Circular gauge dashboard
 - 60 second sensor history buffer
 - Temperature alarm state
 - Network RX/TX monitoring
 - Fan RPM / SSD / motherboard sensor extension

 Data refresh:
 1000ms
*/

#define PCMON_V5_REFRESH_MS 1000
#define PCMON_HISTORY_POINTS 60

#define TEMP_NORMAL_LIMIT_C 70
#define TEMP_WARNING_LIMIT_C 85

typedef enum {
    TEMP_STATUS_NORMAL = 0,
    TEMP_STATUS_WARNING,
    TEMP_STATUS_CRITICAL
} pcmon_temp_status_t;

#endif
