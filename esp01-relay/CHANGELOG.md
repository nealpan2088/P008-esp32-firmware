# ESP-01 继电器模块固件 CHANGELOG

## v1.2 (2026-05-05)

### 新增
- 🆕 **fw-relay-plug-alert 变体**：基于 fw-relay-plug 增加本地报警功能
- 🆕 **`include/alert.h`**：云端阈值解析 + 本地 GPIO 报警输出（LED 快闪）
- 🆕 **心跳响应解析**：从云端 config.thresholds 动态更新本地阈值

### 技术细节
- 报警阈值默认值：电流 0.05~8.0A，温度 -10~50°C，湿度 10~90%
- 报警输出：复用 GPIO2（板载 LED），500ms 快闪
- 状态变化自动恢复，避免重复报警
- WiFi 断网不影响本地报警判断

---

## v1.1 (2026-05-05)

### 修复
- 🔴 **HTTP 客户端冲突导致死机**：`pollCommands()` 中移除 `reportHeartbeat()` 调用（复用全局 http 对象）；`sendCallback()` 改用独立 HTTPClient 实例
- 🔴 **引脚 GPIO0 配网检测后未恢复 OUTPUT 模式**：setup() 检测完按钮后恢复 `pinMode(RELAY_PIN, OUTPUT)`
- 🔴 **POWER_OFF 分支残留 reportHeartbeat()**：只删了 POWER_ON 分支，POWER_OFF 分支漏删

### 作者
- 木子 (代码审查 + 修复) / 潘哥 (硬件测试 + 验收)

---

## v1.0 (2026-05-05)

### 新增
- fw-relay-plug 固件变体（ESP-01 + 继电器模块）
- 指令轮询：`POWER_ON` / `POWER_OFF` / `REBOOT`
- NC 继电器取反逻辑（默认设备断电安全态）
- 心跳上报（60s 间隔）+ 配网（WiFiManager Portal）
- sendCallback 指令执行回执
- 板载 LED 启动闪烁指示
- 看门狗（30s 超时自动重启）

### 技术细节
- 轮询间隔：10 秒（v1.0），5 秒（v1.1 改为 COMMAND_POLL_INTERVAL_MS）
- Flash 占用：~470KB / 761KB
- MicroPython 不提供 OTA 支持
