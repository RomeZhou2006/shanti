# STM32F407VET6 + 逐飞 Wi-Fi SPI 测试

## 工程

- CubeMX 配置：`LandslideWifiHAL/LandslideWifiHAL.ioc`
- Keil 工程：`LandslideWifiHAL/MDK-ARM/LandslideWifiHAL.uvprojx`
- 编译器：ARM Compiler 6.21，Moderate Warnings
- F4 HAL 包：`D:\STM32CubeMX\Repository\STM32Cube_FW_F4_V1.28.3`
- 上位机程序：`../上位机程序/LandslideMonitor.exe`，双击即可运行

在当前机器已完成一次完整重新编译：**0 Error(s), 0 Warning(s)**，输出 `LandslideWifiHAL/MDK-ARM/LandslideWifiHAL/LandslideWifiHAL.hex`。这验证编译和链接，不代表已经在实体模块上通信成功。

## 接线

逐飞模块 2×5 插座按**数字脚号**接，不要按外观猜左右。模块 9 脚接稳定 5 V，7/8 脚接 GND，2 脚不接。

| 模块脚号 | 信号 | MCU 引脚 |
|---:|---|---|
| 1 | MISO | PB14 / SPI2_MISO |
| 3 | SCK | PB13 / SPI2_SCK |
| 5 | MOSI | PB15 / SPI2_MOSI |
| 4 | CS | PE1 / GPIO 输出 |
| 6 | INT | PE9 / GPIO 输入 |
| 10 | RST | PE14 / GPIO 输出 |

SPI2 为模式 0，HSI 16 MHz 时预分频 16，即 1 MHz。PA13/PA14 留给 SWD 下载；PB2 是板载 LED。电源和信号电平须按手中模块版本确认。

## 运行

1. 电脑和 Wi-Fi 模块接入同一热点。关闭之前运行的旧版上位机，双击 `../上位机程序/LandslideMonitor.exe`。新程序监听 UDP 8087（自动发现）和 TCP 8086（测试数据）。
2. `LandslideWifiHAL/Inc/wifi_credentials.h` 已沿用此前工程的 Wi-Fi 名称和密码；这是私密文件，分享工程前自行移除或改用占位值。
3. 打开 Keil 工程并 Rebuild，然后通过 SWD 烧录生成的 HEX。模块会先通过 UDP 寻找上位机，获得电脑当次的 IPv4 后再连接 TCP。上位机会显示四项 `DEMO` 演示值和示例预警等级，并可发送 `PING`、`LED,1/0`、`BUZZER,1/0`。

同一热点重新连接、电脑 IPv4 变化时无需修改固件；换成不同名称或密码的热点，仍需更新 `wifi_credentials.h`。若一直停在“等待模块发现”，检查电脑防火墙是否放行 Python 的 UDP 8087/TCP 8086，以及热点是否允许设备之间的 UDP 广播与互访。逐飞模块的 UDP 广播行为和手机热点的互访能力仍需实物验证。

`DEMO` 是合成测试值，尚未接入真实山体滑坡传感器。协议预留 `SENS,序号,降雨强度,土壤湿度,坡体倾角,位移量` 供以后接入实测值。界面预警等级使用示例阈值，不应据此作实际预警判断。

蜂鸣器控制引脚为 PD12/TIM4_CH1，使用 800 Hz、50% 占空比 PWM；`BUZZER,1/0` 分别开关 PWM 输出。需通过合适的驱动电路连接蜂鸣器，不能把大电流负载直接接到 MCU GPIO。

## 代码位置

- `Src/main.c`：CubeMX 生成的初始化，用户区调用 `wifi_test_app_run()`。
- `Src/wifi_test_app.c`：连接、重连、收发测试消息。
- `Src/wifi_spi.c`：SPI 通信驱动。
- `Inc/app_all.h`：项目常用头文件汇总；Wi-Fi 密码仅在应用源文件中引用。

可以在 CubeMX 打开 `.ioc` 修改引脚并重新生成。已经验证当前版本 CubeMX 重新生成后会保留 `main.c` 用户区、Keil 中的 Wi-Fi 源文件分组、ARM Compiler 6.21 选择及 Moderate Warnings 设置。命令行重生成脚本为 `../generate_configured.mxscript`。
