# CHANGELOG — NodeMCU 1路光耦隔离继电器模块固件

## v1.0 (2026-05-07)
首次发布。

### 功能
- **MQTT 实时通信**：订阅 `p008/{serial}/command` 主题，实时接收指令
- **HTTP 轮询降级**：MQTT 断开时自动切回 HTTP 轮询（5s），恢复后回切 MQTT
- **远程指令控制**: POWER_ON / POWER_OFF / TOGGLE / REBOOT
- **指令轮询**: 每 5 秒 POST 轮询云端待执行指令，执行后回执结果
- **心跳上报**: 每 60 秒上报继电器开关状态 + 在线心跳
- **手动按钮控制**: 短按(<1s)切换继电器，长按(>3s)进入配网模式
- **WiFiManager 配网**: 首次或按住 FLASH 按钮上电进入配网热点
- **自动注册**: 首次心跳自动注册设备到 P008 平台
- **本地报警** (可选): ALARM_ENABLE=1 时支持云端阈值下发+超限LED快闪
- **HMAC 密钥**: BearSSL SHA256 自动生成设备安全密钥
- **TLS MQTT**: 端口 8883，WiFiClientSecure setInsecure() 跳过证书验证
- **高电平触发**: RELAY_PULL=HIGH, RELAY_RELEASE=LOW
- **序列号规范**: `RELAY-NM-{芯片ID}`，通过 build_flags 传入

### 接线对照
| 继电器模块 | NodeMCU |
|-----------|---------|
| IN | D1 (GPIO5) |
| VCC | Vin (5V) |
| GND | GND |

### 通信协议
| 模式 | 通道 | 延迟 | 兜底 |
|------|------|------|------|
| 主模式 | MQTT (1883/8883) | <100ms | HTTP 轮询 |
| 降级 | HTTP POST 轮询 | ~5s | 自动重连 MQTT |
