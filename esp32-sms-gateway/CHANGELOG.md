# esp32-sms-gateway CHANGELOG

## [0.1.0] - 2026-05-08
### 功能
- ESP32 + A7670C 短信报警网关初始版本
- WiFi 连接 + MQTT 订阅 `p008/sms/alert`
- 收到报警后驱动 A7670C 发短信
- 发送结果通过 MQTT 回执到 `p008/sms/status`
- 短信发送队列（最多缓存 10 条，5 秒节流）
- 60 秒心跳上报（信号强度 + SIM 状态）
