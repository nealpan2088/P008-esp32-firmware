# P008 固件开发规范

## 上报间隔规范

### 1. 控制设备（继电器/舵机/执行器类）

- **默认心跳上报间隔：60 秒（1 分钟）**
- 原因：控制指令需要快速响应，1 分钟心跳周期兼顾省电和实时性
- 适用于：`RELAY-*`、`PLC-*`、`ACTUATOR-*` 等序列号的设备
- 后端会根据 `ACTUATOR_REPORT_INTERVAL` 常量自动分配

### 2. 传感器设备

- **默认心跳上报间隔：300 秒（5 分钟）**
- 原因：传感器数据变化慢，省电优先
- 适用于：`DHT22-*`、`DS18B20-*`、`SENSOR-*` 等

### 3. 指令触发自动降间隔

后台在给控制设备下发指令时，会自动将 `reportInterval` 临时降至 5 秒，5 分钟无新指令后自动恢复。固件只需正常解析 HTTP 心跳响应中的 `reportInterval` 字段即可。

### 4. MQTT 优先

- 所有控制类固件应当优先启用 MQTT 模式
- MQTT 模式下指令实时推送（毫秒级延迟），不依赖轮询
- HTTP 轮询作为 MQTT 断线时的降级方案
- 编译时通过 `-D MQTT_ENABLE` 控制开关

### 5. 心跳上报字段规范

所有心跳 POST `/api/v1/devices/{serial}/data` 必须含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `otherData.type` | string | 设备类型，如 `relay`、`sensor` |
| `otherData.relayOn` | bool | 继电器通电状态 |
| `otherData.servoAngle` | int | 舵机当前角度（-1 表示无舵机） |
| `otherData.chipId` | string | 芯片 ID |
| `otherData.firmwareVer` | string | 固件版本 |

## 注意事项

- 不要硬编码上报间隔，始终从云端心跳返回的 `reportInterval` 读取
- 舵机执行完毕必须 `detach()` 省电
- 控制指令执行结果写在 `otherData` 中的对应字段
