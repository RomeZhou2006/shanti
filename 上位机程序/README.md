# 山体滑坡预警系统上位机

双击 `LandslideMonitor.exe` 即可打开，无需单独运行 Python。请先关闭此前运行的 `wifi_test_host.py`，避免 TCP 8086、UDP 8087 端口冲突。

程序会自动寻找同一热点中的 STM32 Wi-Fi 模块，显示降雨强度、土壤湿度、坡体倾角、位移量、示例预警等级和位移趋势。按钮可测试连接、控制板载 LED，以及控制 PD12 蜂鸣器输出；通信记录会显示设备应答。

当前固件发送 `DEMO` 演示数据，尚未接入真实传感器。预警等级按上位机源码 `host/landslide_dashboard.py` 中的示例阈值计算，不能用于实际安全预警。若接入实测数据，固件可发送 `SENS,序号,降雨强度,土壤湿度,坡体倾角,位移量`，并按传感器标定结果重新制定阈值。

先启动本程序，再给模块上电或复位，连接通常需要数秒。若一直未连接，检查手机热点是否允许设备互访、Windows 防火墙是否允许本程序监听，以及模块的热点名称和密码是否正确。更新固件请烧录 `CubeMX_HAL/LandslideWifiHAL/MDK-ARM/LandslideWifiHAL/LandslideWifiHAL.hex`。

如果显示“TCP 已连接”但四项数据仍是 `--`，说明电脑已经接受 Wi-Fi 模块的连接，但尚未收到 STM32 发出的应用数据。新版界面会在 10 秒无数据后提示。先确认已将上述 HEX 下载到板子，并让 MCU 运行：在 Keil 下载完成后按板子 RESET；如果处于 Keil 调试会话且程序暂停，按 F5 继续运行或退出调试。然后观察通信记录是否出现 `line: HELLO,STM32F407` 和 `line: DEMO,...`。仅有 `sent: PING` 或 `sent: LED,1` 不能证明 STM32 已收到命令；应看到 `line: PONG` 或 `line: OK,LED,1`。若仍没有任何 `line:`，保存完整通信记录，并确认实际烧录的 HEX 路径。
