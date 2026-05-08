# esp8266-relay CHANGELOG

## [1.1] - 2026-05-08
### 变更
- 通信协议 HTTPS → HTTP（解决 ESP8266 TLS 握手内存不足）
- API_BASE_URL 改为 http://zghj.openyun.xin/api/v1
- 使用 WiFiClient（普通 HTTP）替代 WiFiClientSecure

### 新增
- speedtest 舵机执行后立即 reportHeartbeat() 上报状态
- CONTRIBUTING.md 固件开发规范

## [1.0] - 2026-05-07
### 功能
- NodeMCU V3 1路光耦继电器 + SG90 舵机
- MQTT + HTTP 双通道通信
- WiFiManager 配网
- SERVO_ANGLE / SERVO_SWEEP / POWER_ON/OFF/TOGGLE 指令
- 60 秒心跳上报
- 5 秒指令轮询
- manual 按钮（短按切换/长按配网）
