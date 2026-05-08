// A7670C 电源管理引脚
// PWR_KEY 通过 10KΩ 电阻上拉到 VCC，模块上电自动开机
#ifndef A7670C_PWR_KEY
#define A7670C_PWR_KEY    -1    // -1 = 硬件上拉，不占用 GPIO
#endif
#ifndef A7670C_BAUD
#define A7670C_BAUD     115200
#endif