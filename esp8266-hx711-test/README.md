# ESP8266 + HX711 称重传感器测试固件

## 用途
验证 HX711 模块 + ESP8266 是否能正确读取重量数据。

## 接线

| HX711 | ESP8266 NodeMCU |
|-------|----------------|
| VCC   | 3.3V           |
| GND   | GND            |
| DT    | GPIO4 (D2)     |
| SCK   | GPIO5 (D1)     |

## 编译烧录

```bash
pio run -t upload
pio device monitor
```

## 标定方法
1. 不放物体，确保读数 0g（踩皮）
2. 放一个已知重量的物体（比如 500g 砝码/一瓶已知重量的水）
3. 调整 `calibration_factor` 直到读数匹配
