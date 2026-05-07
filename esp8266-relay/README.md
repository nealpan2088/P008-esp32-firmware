# NodeMCU 1路光耦隔离继电器固件

> 基于 ESP8266 (NodeMCU) 的光耦隔离继电器模块固件  
> 支持 HTTP 轮询 + MQTT 实时控制双模式，自动降级

---

## 概览

本固件用于 **睿云智感平台**，将 NodeMCU + 1路光耦隔离继电器模块变为可远程控制的 **执行器**。

- **远程控制**：通电（POWER_ON）/ 断电（POWER_OFF）/ 切换（TOGGLE）/ 重启（REBOOT）
- **心跳上报**：每 60 秒上报继电器状态及在线心跳
- **双模通信**：MQTT 实时推送为主，HTTP 轮询为兜底降级
- **手动按钮**：短按切换继电器状态，长按进入配网模式
- **WiFiManager**：首次使用手机连接配网热点
- **自动注册**：首次心跳自动向 P008 平台注册设备

---

## 版本

- **当前版本**: v1.1 → [CHANGELOG.md](./CHANGELOG.md)
- **文件**: `VERSION`

---

## 硬件接线

### 元件清单
| 元件 | 型号 | 数量 |
|------|------|------|
| 主控板 | NodeMCU (ESP8266-12F) | 1 |
| 继电器模块 | 1路光耦隔离继电器模块（高电平触发） | 1 |
| 电源 | 5V/2A Micro USB 供电 | 1 |

### 接线图

```
NodeMCU                  继电器模块
───────                  ─────────
  Vin (5V) ───────────── VCC
  GND     ───────────── GND
  D1 (GPIO5) ────────── IN

负载（如电机、水泵）：
  电源+ ── COM
  电源- ── 负载- 
  负载+ ── NO
```

> **触发极性**：高电平触发（IN=HIGH → 继电器吸合，负载通电）  
> 如果使用低电平触发模块，修改 `config.h` 中 `RELAY_PULL` / `RELAY_RELEASE` 宏定义。

### SG90 舵机接线（可选，v1.1+）

```
NodeMCU                  SG90 舵机
───────                  ─────────
  Vin (5V)  ──────────── 🔴 红色（电源）
  GND       ──────────── 🟤 棕色（GND）
  D2 (GPIO4) ────────── 🟡 橙色（PWM 信号）
```

> ⚠️ **不要从 3.3V 给舵机供电**，SG90 启动瞬间电流可达 700mA，必须用 Vin (5V)。  
> ⚠️ 舵机执行指令后自动 detach 以省电，避免持续 PWM 抖动。

### 指令列表

| 指令 | 参数 | 功能 |
|------|------|------|
| POWER_ON | — | 继电器吸合，负载通电 |
| POWER_OFF | — | 继电器断开，负载断电 |
| TOGGLE | — | 切换继电器状态 |
| REBOOT | — | ESP8266 软重启 |
| SERVO_ANGLE | payload.angle: 0-180 | 舵机转到指定角度 |
| SERVO_SWEEP | — | 舵机来回扫动测试 |

### 更换 IN 信号线须知
继电器模块出厂时 IN 信号线可能存在虚焊或断裂。如发现继电器无响应，请先检查 IN（D1 对应）到继电器 IN 端子的连接是否正常。用万用表二极管档测通断即可。

---

## 编译与烧录

### 环境要求
- VS Code + PlatformIO 插件
- 或 PlatformIO CLI (`pip install platformio`)

### 编译
```bash
cd esp8266-relay

# NodeMCU 标准版（默认，D1/GPIO5）
pio run -e fw-relay-nodemcu

# 带本地报警灯版
pio run -e fw-relay-nodemcu-alert
```

### 烧录（USB 连接后）
```bash
pio run -e fw-relay-nodemcu -t upload
```

### 查看串口日志
```bash
pio device monitor -b 115200
```

---

## 首次使用

### 配网
1. ESP8266 上电
2. 手机搜索 `P008-Relay-XXXX` 热点并连接
3. 浏览器打开 `192.168.4.1`，输入你家 WiFi 密码
4. 配网成功后自动重启并连接云端

> **手动进入配网模式**：按住 FLASH 按钮上电，或长按按钮 >3 秒。

### 验证上线
登录 https://zghj.openyun.xin/controls  
如果看到设备卡片显示"待机"状态，说明已成功接入平台。

---

## 通信协议

### 双模降级策略

| 场景 | 主模式 | 降级 |
|------|--------|------|
| MQTT 已连接 | MQTT 实时下发（<100ms） | —— |
| MQTT 断开 | HTTP 轮询（5s 间隔） | 自动重连 MQTT |
| MQTT & HTTP 均失败 | 命令行等待确认 | 无 |

### MQTT（v1.0+）
- **Broker**: `zghj.openyun.xin:8883`（TLS）
- **Topic**: `p008/{serial}/command`（订阅指令）
- **Topic**: `p008/{serial}/status`（预留，上报状态）

### HTTP 轮询
- **轮询**: `POST /api/v1/devices/{serial}/commands/poll`
- **上报**: `POST /api/v1/devices/{serial}/data`
- **回执**: `POST /api/v1/devices/{serial}/commands/{commandId}/ack`
- **心跳**: 每 60 秒

---

## 设备序列号规范

| 格式 | 说明 | 示例 |
|------|------|------|
| `RELAY-NM-{chipId}` | NodeMCU 标准版 | RELAY-NM-00C06899 |
| `RELAY-PL-{chipId}` | Plug（ESP-01 插排版） | RELAY-PL-00E998E2 |

---

## 项目结构

```
esp8266-relay/
├── include/
│   ├── config.h        # 宏配置（WiFi/MQTT/继电器触发极性/时间）
│   ├── secrets.h       # WiFi 密码（不追踪到 Git）
│   └── VERSION.h       # 编译时版本号（由 build_flags 注入）
├── src/
│   └── main.cpp        # 主程序
├── platformio.ini      # PlatformIO 构建配置
├── README.md           # 本文档
├── CHANGELOG.md        # 变更日志
└── VERSION             # 版本号文件
```

---

## 注意事项

1. **WiFi 密码**：`include/secrets.h` 已加入 `.gitignore`，不要在此文件中提交密码
2. **MQTT 无证书验证**：ESP8266 无 RTC，使用 `WiFiClientSecure.setInsecure()` 跳过证书过期校验
3. **串口日志**：默认 115200 baud，可查看 `[I]`, `[W]`, `[E]` 级别日志
4. **首次上报间隔**：`REPORT_INTERVAL` 默认 60 秒，云端可动态调整
5. **电源稳定性**：继电器吸合瞬间电流较大，建议 5V/2A 以上的 USB 电源
