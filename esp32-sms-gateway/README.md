# P008 — ESP32 + A7670C 短信报警网关

通过 MQTT 接收后端报警消息，驱动 A7670C 4G 模块发送短信到客户手机。

## 架构

```
传感器报警 → 后端检测 → MQTT (p008/sms/alert)
                              ↓
                     家里的 ESP32 (MQTT 订阅)
                              ↓
                      A7670C → 发短信到客户手机
                              ↓
                     MQTT 回执 (p008/sms/status)
```

## 硬件清单

| 硬件 | 数量 | 说明 |
|------|------|------|
| ESP32 开发板 | 1 | 主控，连 WiFi + MQTT |
| A7670C 4G 模块 | 1 | 发短信，需要 2A 独立供电 |
| SIM 卡 | 1 | 支持 4G 的普通手机卡 |
| 4G 天线 | 1 | 模块自带或另配 |

## 接线

| ESP32 | A7670C |
|-------|--------|
| GPIO16 (RX2) | TX |
| GPIO17 (TX2) | RX |
| 5V | VCC（独立 2A 电源）|
| GND | GND |

> ⚠️ A7670C 电流峰值可达 2A，**不要从 ESP32 的 5V 取电**，用独立电源。

## MQTT Topic

| Topic | 方向 | 说明 |
|-------|------|------|
| `p008/sms/alert` | 后端 → ESP32 | 报警短信内容 |
| `p008/sms/status` | ESP32 → 后端 | 发送状态回执 |

## 报警消息格式

```json
{
  "to": "13800138000",
  "message": "【睿云智感】冷库A温度超标！当前: 12.5°C",
  "id": "alert-xxx"
}
```

## 编译上传

```bash
cd esp32-sms-gateway
pio run -t upload
```

## 串口监视

```bash
pio device monitor -b 115200
```
