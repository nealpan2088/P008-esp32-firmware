#ifndef LOG_H
#define LOG_H

#include <Arduino.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#define LOG_E(tag, fmt, ...) do { if (LOG_LEVEL >= 1) Serial.printf("[E][%s] " fmt "\r\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_W(tag, fmt, ...) do { if (LOG_LEVEL >= 2) Serial.printf("[W][%s] " fmt "\r\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_I(tag, fmt, ...) do { if (LOG_LEVEL >= 3) Serial.printf("[I][%s] " fmt "\r\n", tag, ##__VA_ARGS__); } while(0)
#define LOG_D(tag, fmt, ...) do { if (LOG_LEVEL >= 4) Serial.printf("[D][%s] " fmt "\r\n", tag, ##__VA_ARGS__); } while(0)

#endif
