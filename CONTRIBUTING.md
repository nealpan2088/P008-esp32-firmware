# P008 固件开发规范

> 本篇是简化版，**完整统一规范请参看 P008-env-monitor 仓库的 CONTRIBUTING.md**。
> 以下仅列固件专属内容，与主规范有冲突时以主规范为准。

---

## 固件目录结构

```
固件目录/
├── VERSION              # 纯版本号，如 `1.0`
├── CHANGELOG.md         # 变更记录
├── README.md            # 硬件接线 + 编译说明
├── platformio.ini       # PlatformIO 配置（含 lib_deps）
├── src/
│   └── main.cpp         # 主程序
└── include/
    ├── config.h         # 配置常量（WiFi/MQTT/引脚，使用 #ifndef 允许用户覆盖）
    └── log.h            # 日志宏
```

## 通用代码规范

- 舵机执行完毕必须 `detach()` 省电
- 控制指令执行结果写入心跳的 `otherData` 对应字段
- `config.h` 中所有常量使用 `#ifndef` 包裹，允许编译时 `-D` 覆盖
- 网络请求必须设超时（HTTP 5 秒、WiFi 30 秒）
- HTTP 连接失败需有重试机制（至少 3 次）

## 各固件差异

| 固件 | 目标板 | 通信 | 心跳间隔 | 特殊约束 |
|------|--------|------|---------|---------|
| `esp8266-relay` | NodeMCU V3 | MQTT + HTTP | 60s | HTTPS 不可用（TLS 内存不足），用明文 HTTP |
| `esp8266-sensor` | NodeMCU V3 | HTTPS | 300s | 无特殊约束 |
| `esp01-relay` | ESP-01 | HTTPS | 60s | 内存最小，精简库依赖 |
| `sct-current-test` | NodeMCU V3 | HTTPS | 按需 | 测试用途 |
| `esp32-sms-gateway` | ESP32 DevKit | MQTT only | 无 HTTP 心跳 | 纯 MQTT，A7670C 需 2A 独立供电 |

## 快速参考

| 内容 | 在哪里 |
|------|--------|
| 完整规范（版本管理/通信协议/后端/前端/数据库） | [P008-env-monitor CONTRIBUTING.md](https://github.com/nealpan2088/P008-env-monitor/blob/main/CONTRIBUTING.md) |
| 心跳字段完整列表 | 同上 |
| MQTT Topic 格式 | 同上 |
| 上报间隔规则 | 同上 |
| 指令格式和类型 | 同上 |
| 提交规范 | 同上 |
| 发布检查清单 | 同上 |
