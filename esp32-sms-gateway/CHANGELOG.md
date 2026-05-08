# esp32-sms-gateway CHANGELOG

## [0.1.1] - 2026-05-08
### 新增
- 中文短信支持（TEXT 模式 + UCS2 编码 + AT+CSMP 方案）
- UTF-8 → UCS2 HEX 编码函数
- WiFiManager 配网（热点 "P008-SMS-Gateway"，无密码）

### 修复
- PDU 模式改为 TEXT 模式 + UCS2 编码（实测 A7670C 兼容方案）
- UART2 引脚 GPIO18(RX)/GPIO19(TX) 避免引脚冲突
- MQTT 改回 8883 TLS（阿里云安全组已开放）
- 去掉 WiFi 热点密码
- s endSms 超时延长至 15 秒

### 技术细节
- AT 指令序列：AT+CMGF=1 → AT+CSCS="UCS2" → AT+CSMP=17,167,2,25 → AT+CMGS="<UCS2手机号>"
- 手机号和短信内容均需 UCS2 HEX 编码
- 输入内容后不要换行，直接 Ctrl+Z 结束
- 发送间隔 5 秒批量节流，队列最大 10 条
- WiFi 连接 + MQTT 订阅 `p008/sms/alert`
- 收到报警后驱动 A7670C 发短信
- 发送结果通过 MQTT 回执到 `p008/sms/status`
- 短信发送队列（最多缓存 10 条，5 秒节流）
- 60 秒心跳上报（信号强度 + SIM 状态）
