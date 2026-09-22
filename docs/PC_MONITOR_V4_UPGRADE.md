# PC MONITOR v4.0 Upgrade

## Added
- Gauge style dashboard layout
- CPU/GPU/MEM visual meters
- Temperature alarm state
- Network speed telemetry preparation

## Display
CPU:
  Usage % + Temperature

GPU:
  Usage % + Temperature

MEM:
  Used MB / Total MB + %

Network:
  RX KB/s
  TX KB/s

## Refresh
All monitor values are designed for 1000ms update interval.

## Host protocol extension
Future STAT packet extension:

STAT|CPU|GPU|CPU_TEMP|GPU_TEMP|MEM|MEM_USED|MEM_TOTAL|RX_KBPS|TX_KBPS
