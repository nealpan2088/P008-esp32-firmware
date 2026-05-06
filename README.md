# P008 ESP8266/ESP32 传感器固件仓库

> 睿云智感环境监测系统硬件固件合集
> 基于 PlatformIO + ESP8266 RTOS / ESP32 Arduino

---

## 固件列表

| 目录 | 芯片 | 功能 | 版本 |
|------|------|------|------|
| `esp8266-sensor/` | ESP8266 | DHT22/DS18B20 温湿度传感器 + 电流检测 + 门磁 | v3.4 |
| `esp01-relay/` | ESP-01 | 继电器远程控制插排固件 | v1.1 |
| `sct-current-test/` | ESP8266 | SCT-013 电流钳检测测试 | v1.0 |
| `a7670-notifier/` | Arduino/ESP | A7670C 4G 短信通知 Demo | 开发中 |

## 开发环境

- **IDE**: VS Code + PlatformIO
- **框架**: Arduino框架 for ESP8266/ESP32
- **编译**:
  ```bash
  cd esp8266-sensor
  pio run -e dht22-plug    # 插电版 DHT22
  pio run -e battery       # 电池版 DHT22
  pio run -e ds18b20-plug  # 插电版 DS18B20
  pio run -t upload        # 烧录
  ```

## 详细文档

见各固件目录下的 `README.md` + `CHANGELOG.md`。

## 版本历史

| 日期 | 版本 | 说明 |
|------|------|------|
| 2026-05-06 | - | 独立仓库初始化，从 P008 项目拆分 |
| 2026-05-02 | v3.4 | 定风版定型 + 电池版 v3.2 |
| 2026-04-29 | v3.1 | 安全密钥 SHA256 + 动态 reportInterval |

---

**注意**：
- `a7670-notifier/` 为 Demo 阶段，需配合 ESP32 实现完整 MQTT 短信网关
- 烧录电池版固件前确保 GPIO16→RST 跳线断开
- 详细硬件指南见 `docs/` 目录
