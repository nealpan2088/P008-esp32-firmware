# P008 — ESP-01 继电器模块固件

> 固件版本：v1.1 | 项目版本：0.8.3 | 编译变体：fw-relay-plug / fw-relay-plug-alert

## 概述

基于 ESP-01/01S + 继电器模块的远程控制设备。通过 P008 环境监测平台远程控制继电器通断，支持：

- **POWER_ON** — 断开继电器（设备断电，安全默认态）
- **POWER_OFF** — 吸合继电器（设备通电）
- **REBOOT** — 远程重启模块

**重要：** 本模块使用 NC（常闭）型继电器，NC-COM 默认导通。为安全起见，固件逻辑已取反：
- `POWER_ON` = 断开 NC-COM → **设备断电**（通电后默认状态）
- `POWER_OFF` = 吸合 NC-COM → **设备通电**

## 文件结构

```
hardware/esp01-relay/
├── README.md          ← 本文档（固件说明、引脚、编译、版本）
├── CHANGELOG.md       ← 变更日志
├── platformio.ini     ← PlatformIO 编译配置（包含 ESP-01 和 NodeMCU 两个环境）
├── include/
│   ├── config.h       ← 硬件配置（引脚、WiFi、API、密钥）
│   └── log.h          ← 串口日志宏（LOG_LEVEL 控制输出级别）
├── src/
│   └── main.cpp       ← 主程序（配网、心跳、指令轮询、继电器控制）
└── docs/              ← （保留旧版 README 备份）
```

### 关键文件说明

| 文件 | 作用 | 开发者需关注 |
|------|------|------------|
| `platformio.ini` | 编译环境定义 | 添加新 variant 时编辑 |
| `include/config.h` | 引脚、超时、硬件常量 | 改引脚定义时注意与 platformio.ini 的 `build_flags` 同步 |
| `src/main.cpp` | 全部业务逻辑 | 新增指令类型时编辑 |

## 引脚定义

| 引脚 | 功能 | 信号 | 说明 |
|------|------|------|------|
| GPIO0 | 继电器控制 | HIGH=断开，LOW=吸合 | 模块板载已接好三极管驱动 |
| GPIO0 | 配网按钮检测 | 上电短接 GND 3 秒 | 与继电器复用同一个 GPIO（检测后恢复 OUTPUT） |
| GPIO2 | 板载 LED | LOW=亮，HIGH=灭 | 心跳指示 |
| TX/RX | 串口调试 | 115200 baud | 日志输出 |

## 接线与烧录

### 烧录接线（USB-TTL → ESP-01）

| USB-TTL | → | ESP-01 |
|---------|---|--------|
| 3.3V | → | VCC（引脚 8） |
| 3.3V | → | CH_PD/EN（引脚 6） |
| GND | → | GND（引脚 1） |
| GND | → | GPIO0（引脚 3）—— 拉低进入下载模式 |
| TX | → | RX（引脚 4） |
| RX | → | TX（引脚 5） |

**推荐工具：** ESP Prog v1.0 编程器（带 2×4 弹簧座，直接插 ESP-01）

### 烧录步骤

```bash
cd hardware/esp01-relay

# 确保下载模式：GPIO0 接 GND（ESP Prog 跳线帽短接）
pio run -e fw-relay-plug -t upload --upload-port COM8

# 烧完后：拔掉 GPIO0→GND → 重新上电
```

### 运行接线（烧录完成后）

```
               USB 充电器 (5V)
                  │
               ┌──┴──┐
               │     │
            ┌──┤     └────── → 继电器模块 VCC
            │  └──────────── → 负载(+)
            │
        继电器模块 GND ─── USB GND
            │
        继电器 NC ───── 负载(-)
        继电器 COM ───── USB GND
```

## 首次配网

1. 手机连 **P008-Relay** 热点（AP 模式）
2. 浏览器打开 192.168.4.1
3. 选择你家 WiFi，输入密码
4. 模块自动重启连接 P008 平台

**更换 WiFi：** 上电时按住 GPIO0（模块上的按钮）3 秒 → 进入配网模式

## 编译环境

| 参数 | 值 |
|------|-----|
| 芯片 | ESP8266EX (ESP-01/01S) |
| Flash | 1MB (8Mbit) |
| 框架 | Arduino |
| 串口波特率 | 115200 |
| 上传速度 | 19200（115200 会报 Invalid head of packet） |
| Flash 模式 | dout |

## 环境变量（build_flags）

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `RELAY_PIN` | 0 | GPIO0 — 继电器控制 |
| `LED_BUILTIN` | 2 | GPIO2 — 板载 LED |
| `RELAY_ON` | HIGH | 继电器断开（设备断电，安全态） |
| `RELAY_OFF` | LOW | 继电器吸合（设备通电） |
| `COMMAND_POLL_INTERVAL_MS` | 5000 | 指令轮询间隔（ms） |
| `HEARTBEAT_INTERVAL_MS` | 60000 | 心跳上报间隔（ms） |
| `WDT_TIMEOUT_US` | 30000000 | 看门狗超时（30s） |
| `DEVICE_SERIAL_PREFIX` | `RELAY-PL-` | 设备序列号前缀 |
| `LOG_LEVEL` | 3 | 日志级别（3=INFO, 4=DEBUG） |
| `HW_SECRET` | `P008@2026!SecretKey` | HMAC 密钥 |

## 指令协议

### 支持指令

| 指令 | 固件动作 | 电气效果 |
|------|---------|---------|
| `POWER_ON` | `digitalWrite(RELAY_PIN, HIGH)` | NC-COM 断开，设备断电 |
| `POWER_OFF` | `digitalWrite(RELAY_PIN, LOW)` | NC-COM 吸合，设备通电 |
| `REBOOT` | `ESP.restart()` | 模块重启 |

### 指令优先级

- **REBOOT 优先**：即使同时有其他 PENDING 指令，REBOOT 立即重启
- **POWER_ON > POWER_OFF**：后收到的指令覆盖之前的

## 开发规范

### P008 设计原则符合情况

| 原则 | 状态 |
|------|------|
| 固件版本化（`firmwareVer`） | ✅ otherData 中上报 |
| 数据兼容（新增字段不破坏旧设备） | ✅ relayOn 放 otherData |
| 协议版本化（/v1/） | ✅ API 路径 v1 |
| 功能开关（配置驱动） | ✅ platformio.ini build_flags 控制 |

### 已知问题 / 后续改进

1. **HW_SECRET 硬编码** — 当前在 config.h 中给默认值，和传感器固件保持一致。如需增强可改为从 build_flags 传入
2. **无 OTA** — ESP-01 Flash 太小（~500KB 可用），不支持 OTA
3. **无电压反馈** — 继电器模块无 ADC 采样，无法检测负载是否真实上电
4. **LED 与继电器共用 GPIO0** — 配网按钮检测需临时切换引脚模式

## 版本历史

详见 [CHANGELOG.md](./CHANGELOG.md)

---

*P008 环境监测系统 · ESP-01 继电器模块固件 v1.1*
