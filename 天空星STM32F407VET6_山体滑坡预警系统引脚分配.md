# 天空星 STM32F407VET6 青春版：山体滑坡预警系统引脚分配

> 2026-11-12 原理图换脚版见 [传感器与其他原理图 PDF](SCH_Schematic1_2-传感器与其他_2026-11-12.pdf)、[LaTeX 引脚设计](pin_design_schematic_2026-11-11.tex)及[修订版引脚设计 PDF](pin_design_schematic_2026-11-12.pdf)。本文件后半段保留早期资源候选表；实际接线以新版原理图和 LaTeX 表为准。USART1 的 TX_1/RX_1 使用 PB6/PB7，OLED/MHT04 的 I²C1 使用 PB8/PB9；SWD 使用 PA13/PA14。

适用范围：**单独使用天空星 STM32F407VET6 青春版核心板**，不插“筑基学习板”。下表中的 `PAx/PBx/...` 是芯片 GPIO 名称，接线时按板上丝印寻找同名排针；不是芯片封装脚号。青春版的两排 40P 外扩排针默认未焊接，使用前需焊接。

**当前原理图：4 路 MCU 原生串口；SPI1 给 TFT，SPI2 给 Wi-Fi，另预留 1 路 GPIO 模拟 SPI。** USART1 的应用引脚改用 PB6/PB7；板载调试口 PA9/PA10 不作为独立第 5 路串口。保留 TF 卡、USB 和 SWD 下载引脚 PA13/PA14，也不占用 PB3/PB4/PB5 调试复用脚。

## 总方案

| 类别 | 数量 | 引脚 | 配置 / 参考用途 |
|---|---:|---|---|
| 硬件串口 | 4 路 | USART1/2/3/6 | USART1 PB6/PB7 给 TX/RX_1；USART2/3/6 给 TX/RX_2、_3、_4 |
| 硬件 I²C | 2 路 | PB8/PB9；PB10/PB11 | I²C1 PB8/PB9 共用 OLED/MHT04；I²C2 PB10/PB11 给 IMU |
| SPI 接口 | 3 路 | 硬件 SPI1：PA5/PA6/PA7；硬件 SPI2：PB13/PB14/PB15；GPIO 模拟 SPI：PE10/PE11/PE12 | 第 3 路是软件驱动，适合低速传感器；每组独立片选 |
| ADC | 6 路 | PC0～PC5 | 雨量/土壤水分/位移等模拟输出；按实际量程设计调理电路 |
| SPI 片选 GPIO | 2 路已规划 | PE1、PE2 | PE1 是 Wi-Fi CS；PE2 预留给软件 SPI；TFT 无 CS，PE0 已用于 TFT_DC |
| 其他 GPIO / PWM | 按原理图 | PE0、PE3、PE5～PE8、PD10～PD15 | TFT_DC/RES/BLK、IMU INT1、按键、蜂鸣器和 RGB 已分别占用；详见 LaTeX 表 |

### 已确定的模块占用

| 模块 | 通信引脚 | 其他 MCU 引脚 | 供电与模式 |
|---|---|---|---|
| 逐飞 Wi-Fi SPI | SPI2：PB13 SCK、PB14 MISO、PB15 MOSI | PE1 CS、PE9 INT、PE14 RST | 模块 +5 V、共地；详细脚号见下文 |
| IMU（按提供的 ICM42688 模块资料） | I²C2：PB10 SCL、PB11 SDA | PE5 INT1 | 模块 VCC 接 3.3 V、共地；AD0 接 GND，CS 由模块内部上拉 |
| 有源蜂鸣器驱动 | 无 | PD12 | GPIO 控制外部驱动电路 |

当前原理图中，除 OLED/MHT04 共用 I²C1 之外，各功能未重叠，且均避开 PA13/PA14 下载调试引脚。当前 CubeMX 工程已配置 Wi-Fi SPI 和 PD12；USART1 PB6/PB7、I²C1 PB8/PB9、I²C2 PB10/PB11 等仍需在 CubeMX 中启用。

## 4 路原生串口

| 序号 | 外设 | TX（MCU 发送） | RX（MCU 接收） | 复用 | 接线说明 |
|---|---|---|---|---|---|
| 1 | USART1 | PB6 | PB7 | AF7 | 外扩排针上的 TX_1/RX_1；PA9/PA10 未引至两排 40P |
| 2 | USART2 | PD5 | PD6 | AF7 | 可接 3.3 V TTL 模块，或经 RS485 收发器接远端传感器 |
| 3 | USART3 | PD8 | PD9 | AF7 | 备用传感器 / 通信模块 |
| 4 | USART6 | PC6 | PC7 | AF8 | 无线通信 / 备用串口 |

串口接线是 **MCU TX → 对方 RX、MCU RX ← 对方 TX、GND 共地**。这 4 路都是 MCU 逻辑电平接口，不能直接接 RS232 ± 电压或 RS485 A/B 线；需对应收发器。

板载 2×5 调试口的 PA9/PA10 与外扩排针 PB6/PB7 是 USART1 的两组复用引脚，本方案只选择 PB6/PB7 用于应用串口。板载串口 PA9/PA10 不再参与应用通信，也不另算一路串口。烧录、调试继续使用 PA13/PA14 的 SWD；ROM 串口下载的支持引脚不会因 CubeMX 的应用引脚复用而改变。

## I²C

| 总线 | SCL | SDA | 复用 | 建议 |
|---|---|---|---|---|
| I²C1 | PB8 | PB9 | AF4 | OLED 与 MHT04 共线；可挂多个地址不冲突的设备 |
| I²C2 | PB10 | PB11 | AF4 | ICM42688 模块；与 I²C1 分开 |

两根线上需按模块实际情况配置到 **3.3 V 的上拉电阻**；所提供的 IMU 模块原理图已有 SDA、SCL 各 10 kΩ 上拉。I²C 是总线，多个设备通常共用同一对 SCL/SDA；不同总线可解决相同地址设备冲突。I²C3 的 PA8/PC9 理论上可用，但 PC9 接板载 TF 卡线路，本方案不分配。

### ICM42688 模块 I²C 接线

按所提供的《ICM42688模块规格书》和 `SCH42688new.pdf` 中的模块信号名接线；下面的 `VCC` 是模块排针，不是芯片裸片的 VDDIO。

| 模块信号 | 连接 | 说明 |
|---|---|---|
| VCC、GND | 核心板 3.3 V、GND | 规格书允许 VCC 供电 3.3～5 V；本方案统一用 3.3 V |
| SCL、SDA | PB10、PB11 | I²C2，AF4；模块原理图已有两根线各 10 kΩ 上拉到模块内部 3V3 |
| AD0/MISO | GND | 选择 I²C 7 位地址 `0x68`；不能悬空 |
| CS | 不接外部线 | 模块原理图 R6 10 kΩ 已将 CS 上拉到内部 3V3；不要接 GND |
| INT1 | PE5 | 可配置为 EXTI5；触发沿须与传感器中断极性设置一致 |
| INT2 | 暂不接 | 当前方案不使用第二路中断或同步功能 |

`SCH42688new.pdf` 的标题仍写着 `lsm6dsv`，而配套规格书写 `ICM42688`。电源、CS 上拉和接头网络按该 PDF 记录；正式编写传感器驱动前，应核实实物芯片型号，不能只凭文件名判断。

## SPI：两路硬件，一路软件

| 总线 | SCK | MISO | MOSI | 模式 | 片选建议 | 注意 |
|---|---|---|---|---|---|---|
| SPI1 | PA5 | PA6 | PA7 | AF5 | TFT 无 CS | PE0 是 TFT_DC；仅写屏幕可不接 MISO |
| SPI2 | PB13 | PB14 | PB15 | AF5 | PE1 | 本次 Wi-Fi SPI 模块测试使用；板上预留 SPI Flash 默认未贴装 |
| 第 3 路 SPI | PE10 | PE11 | PE12 | GPIO 模拟时序 | PE2 | 独立的外部接线接口，但不占用 MCU 的 SPI3 控制器；适合低速设备 |

片选 CS/NSS 可用普通推挽 GPIO，不要求占用硬件 NSS 引脚；同一总线多个从设备各用独立 CS。当前 TFT 模块没有 CS，故 SPI1 不另分配片选引脚；PE0 已固定为 TFT_DC。若自行焊接板载 SPI Flash，先对照该板原理图核实其线路与这里的 SPI2 配置。

### 本次逐飞 Wi-Fi SPI 模块接线

按模块 2×5 插座的**数字脚号**接线，不要只凭排针方向推断。此次测试占用上表的 SPI2 和片选 PE1，并新增 PE9、PE14 两个控制引脚。

| 模块脚号 | 模块信号 | STM32F407 引脚 / 连接 |
|---|---|---|
| 1 | MISO | PB14，SPI2_MISO |
| 3 | SCK | PB13，SPI2_SCK |
| 5 | MOSI | PB15，SPI2_MOSI |
| 4 | CS | PE1，GPIO 输出 |
| 6 | INT | PE9，GPIO 输入 |
| 10 | RST | PE14，GPIO 输出 |
| 7、8 | GND | 与核心板共地 |
| 9 | +5V | 稳定 5 V 供电，模块峰值电流按官方资料预留余量 |
| 2 | NC | 不连接 |

PE9 和 PE14 未分配给前述任何外设，且不占用 PA13/PA14 下载引脚。该模块资料说明电源为 5 V；**SPI/INT/RST 的逻辑电平还需按实物版本原理图确认**，不得把 5 V 信号直接送入未经确认可耐受的 MCU 引脚。测试程序用 SPI2 模式 0、1 MHz 起步。


## ADC 与通用 IO

| 用途 | 引脚 | 对应通道 / 说明 |
|---|---|---|
| ADC1：模拟传感器 1～6 | PC0、PC1、PC2、PC3、PC4、PC5 | ADC123_IN10～IN13；ADC12_IN14～IN15。建议统一由 ADC1 采集 |
| 备用 ADC | PB0、PB1 | ADC12_IN8、ADC12_IN9 |
| 外部中断输入 | PE5 | 已分配 IMU INT1 / EXTI5；PE6 给 TFT_BLK，PE7/PE8 给按键 |
| 驱动输出 | PD12、PD13～PD15 | PD12 蜂鸣器，PD13～PD15 RGB；PE3 给 TFT_RES，PD10/PD11 给按键 |
| SPI 片选 | PE1、PE2 | PE1 给 Wi-Fi；PE2 预留给 GPIO 模拟 SPI；PE0 给 TFT_DC |

ADC 输入必须在所用模拟参考电压范围内，常见接法按 0～3.3 V 设计；5 V、4～20 mA、电桥或长线传感器应先用分压/采样电阻、滤波、保护及必要隔离，不能直接进 ADC。模拟信号尽量与继电器、蜂鸣器走线分开。


## 当前普通 IO 中具备硬件 PWM 能力的引脚

下表列出部分 GPIO 的硬件 PWM 能力与新版原理图中的实际占用。PWM 是可选复用功能；同一引脚不能同时承担不同外设功能。

| 引脚 | 定时器通道 | 复用 | 当前分配与使用条件 |
|---|---|---|---|
| PE5 | TIM9_CH1 | AF3 | 已分配给 IMU INT1；若改用 PWM，需先释放该中断输入 |
| PE6 | TIM9_CH2 | AF3 | 已分配 TFT_BLK，可配置 PWM 调背光 |
| PE8 | TIM1_CH1N | AF1 | 已分配 KEY2；若改用 PWM，需释放按键功能，这是互补输出 |
| PD12 | TIM4_CH1 | AF2 | 已分配蜂鸣器驱动，可按蜂鸣器类型选择 GPIO 或 PWM |
| PD13 | TIM4_CH2 | AF2 | 已分配 RGB_PWM1，使用 PWM |

PE3、PE7、PD10、PD11 没有定时器 PWM 输出通道。PE5/PE6 同属 TIM9，PD12～PD15 同属 TIM4；同一定时器的各通道可独立设占空比，但共用周期/频率。依据：[ST STM32F407VE 数据手册的 LQFP100 引脚及复用功能表](https://www.st.com/resource/en/datasheet/stm32f407ve.pdf)。

## 板载占用和不可同时使用的功能

| 引脚 | 板载功能 / 本方案处理 |
|---|---|
| PA0 | 板载用户按键，保留；不拿来做 UART4_TX |
| PB2 | 板载绿色 LED / BOOT1，保留 |
| PA11/PA12 | Type-C USB-FS D−/D+，保留 USB 功能 |
| PA13/PA14 | SWDIO/SWCLK，保留下载调试 |
| PB3/PB4/PB5 | 留给 SWO/JTAG 等调试相关功能；本方案不占用 |
| PC8～PC12、PD2、PD3 | 板载 TF 卡相关线路，全部留给 TF 卡；**不分配给其他外设** |
| PH0/PH1 | 板载 8 MHz 高速晶振，不作 GPIO |
| PC14/PC15 | 预留低速晶振焊盘；若将来贴装 32.768 kHz 晶振须保持空闲 |

## 实施提示

1. STM32CubeMX 选择 **STM32F407VETx**，先开启 SWD 下载，再按表启用 USART1/2/3/6、I2C1/2、SPI1/2、ADC1 和按需启用 SDIO。第 3 路 SPI 用 GPIO 软件驱动，不在 CubeMX 中配置为 SPI3。
2. 检查 CubeMX 没有自动改动所选引脚。各原生串口先按异步模式，TX/RX 足够；RS485 的 DE/RE 另用 GPIO。接入 IMU 时还需启用 I²C2（PB10/PB11）及 PE5 输入/EXTI5；当前测试固件尚未实现 IMU 采集。
3. 新版原理图中，PB6/PB7 专供 USART1，PB8/PB9 共供 OLED/MHT04 的 I²C1；这些共享只发生在同一条 I²C 总线上。PE0 已用于 TFT_DC，不同时作为 SPI1 片选。下文早期候选 GPIO/PWM/ADC 表仅列芯片能力，不能与新版原理图的实际占用叠加计数。

## 核对来源

- [立创：天空星核心板版本与板载资源](https://wiki.lckfb.com/zh-hans/fdb/skystar.html)
- [立创：天空星核心板原理说明](https://wiki.lckfb.com/zh-hans/tkx/hardware/schematic.html)
- [立创：STM32 版天空星硬件资源摘要](https://wiki.lckfb.com/zh-hans/fdb/micropython/introduction.html)
- [ST：STM32F407VE 数据手册，LQFP100 引脚和复用功能表](https://www.st.com/resource/en/datasheet/stm32f407ve.pdf)

本表是基于官方芯片数据手册和板级资料的接线方案，尚未在实物板上逐路通电验证；正式接线前再按手中 PCB 丝印与版本原理图复核。
