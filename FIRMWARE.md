# P008 ESP8266 固件一览

## 固件总表

| 固件目录 | 目标板 | 功能 | 版本 | 通信 |
|---------|--------|------|------|------|
| `esp8266-relay` | NodeMCU V3 | 1路继电器 + SG90 舵机 | v1.1 | MQTT + HTTP |
| `esp8266-sensor` | NodeMCU V3 | DHT22/DS18B20/MQ-135/门磁 | v3.4 | HTTPS |
| `esp01-relay` | ESP-01 | 1路继电器（最小化） | v1.1 | HTTPS |
| `sct-current-test` | NodeMCU V3 | SCT-013-000 电流测试 | v1.0 | HTTPS |
| `esp32-sms-gateway` | ESP32 DevKit | 4G 短信报警网关 (A7670C) | v0.1.0 | WiFi + MQTT |

## 快速链接
- [esp8266-relay 文档](esp8266-relay/README.md)
- [esp8266-sensor 文档](esp8266-sensor/README.md)
- [esp01-relay 文档](esp01-relay/README.md)
- [sct-current-test 文档](sct-current-test/README.md)
- [esp32-sms-gateway 文档](esp32-sms-gateway/README.md)
- [固件开发规范](CONTRIBUTING.md)

## 硬件规范

| 项目 | 值 |
|------|-----|
| 默认心跳间隔 | 60 秒（传感器）/ 60 秒（执行器） |
| 指令轮询间隔 | 5 秒 |
| HTTP 超时 | 5 秒 |
| WiFi 超时 | 30 秒 |
| 通信优先 | MQTT > HTTP 轮询 |
| 上报 URL | `POST /api/v1/devices/{serial}/data` |
