"""山体滑坡预警系统上位机。运行后自动监听模块发现与监测数据。"""

import argparse
import queue
import time
import tkinter as tk
from collections import deque

from wifi_test_host import DISCOVERY_PORT, PORT, TestServer, local_ipv4_addresses


METRICS = (
    ("降雨强度", "mm/h", "rain"),
    ("土壤湿度", "%", "soil"),
    ("坡体倾角", "°", "tilt"),
    ("位移量", "mm", "displacement"),
)
# 示例阈值：传感器型号、标定和课设方案确定后必须重新设定。
LIMITS = {
    "rain": (20, 40, 60),
    "soil": (60, 75, 90),
    "tilt": (8, 15, 25),
    "displacement": (5, 10, 20),
}
LEVELS = (
    ("正常", "#16805d"),
    ("关注", "#b77911"),
    ("预警", "#d75a19"),
    ("危险", "#b4323a"),
)


def parse_telemetry(line):
    parts = line.split(",")
    if len(parts) != 6 or parts[0] not in ("DEMO", "SENS"):
        return None
    try:
        sequence, rain, soil, tilt, displacement = map(int, parts[1:])
    except ValueError:
        return None
    if not (0 <= sequence <= 4294967295 and 0 <= rain <= 500 and
            0 <= soil <= 100 and 0 <= tilt <= 90 and 0 <= displacement <= 1000):
        return None
    return parts[0], sequence, dict(rain=rain, soil=soil,
                                    tilt=tilt, displacement=displacement)


def evaluate_level(values):
    return max(
        (level for name, limits in LIMITS.items()
         for level, threshold in enumerate(limits, 1)
         if values[name] >= threshold),
        default=0,
    )


def run_dashboard(server):
    root = tk.Tk()
    root.title("山体滑坡预警系统 · 监测上位机")
    root.geometry("1060x760")
    root.minsize(850, 650)
    root.configure(bg="#edf2f6")

    header = tk.Frame(root, bg="#17324d", padx=22, pady=16)
    header.pack(fill="x")
    tk.Label(header, text="山体滑坡预警系统", bg="#17324d", fg="white",
             font=("Microsoft YaHei UI", 20, "bold")).pack(anchor="w")
    tk.Label(header, text=f"Wi-Fi 自动发现 UDP {DISCOVERY_PORT}  ·  数据连接 TCP {server.port}",
             bg="#17324d", fg="#bcd0e0", font=("Microsoft YaHei UI", 10)).pack(anchor="w", pady=(4, 0))

    body = tk.Frame(root, bg="#edf2f6", padx=20, pady=14)
    body.pack(fill="both", expand=True)
    ip_text = "、".join(local_ipv4_addresses()) or "未检测到"
    status_var = tk.StringVar(value="等待 Wi-Fi 模块连接")
    source_var = tk.StringVar(value="尚无数据")
    tk.Label(body, textvariable=status_var, bg="#edf2f6", fg="#29465d",
             font=("Microsoft YaHei UI", 12, "bold")).pack(anchor="w")
    tk.Label(body, text=f"本机 IPv4：{ip_text}    模块需与电脑接入同一热点",
             bg="#edf2f6", fg="#607387", font=("Microsoft YaHei UI", 9)).pack(anchor="w", pady=(2, 10))

    metric_grid = tk.Frame(body, bg="#edf2f6")
    metric_grid.pack(fill="x")
    metric_vars = {}
    for index, (title, unit, key) in enumerate(METRICS):
        row, col = divmod(index, 2)
        metric_grid.columnconfigure(col, weight=1)
        card = tk.Frame(metric_grid, bg="white", padx=18, pady=12,
                        highlightbackground="#dce5ec", highlightthickness=1)
        card.grid(row=row, column=col, sticky="ew", padx=5, pady=5)
        tk.Label(card, text=title, bg="white", fg="#61778a",
                 font=("Microsoft YaHei UI", 10)).pack(anchor="w")
        value = tk.StringVar(value="--")
        metric_vars[key] = value
        tk.Label(card, textvariable=value, bg="white", fg="#17324d",
                 font=("Microsoft YaHei UI", 22, "bold")).pack(side="left", pady=(4, 0))
        tk.Label(card, text=unit, bg="white", fg="#61778a",
                 font=("Microsoft YaHei UI", 10)).pack(side="left", padx=8, pady=(14, 0))

    alarm_row = tk.Frame(body, bg="#edf2f6", pady=10)
    alarm_row.pack(fill="x")
    alarm = tk.Label(alarm_row, text="等待数据", bg="#667a8a", fg="white",
                     font=("Microsoft YaHei UI", 15, "bold"), padx=26, pady=12)
    alarm.pack(side="left")
    tk.Label(alarm_row, textvariable=source_var, bg="#edf2f6", fg="#526a7d",
             font=("Microsoft YaHei UI", 10)).pack(side="left", padx=14)
    tk.Label(body, text="预警等级采用演示阈值，仅用于课设联调；接入实测传感器后须重新标定。",
             bg="#edf2f6", fg="#9a5e28", font=("Microsoft YaHei UI", 9)).pack(anchor="w")

    controls = tk.LabelFrame(body, text="设备控制", bg="#edf2f6", fg="#29465d",
                             font=("Microsoft YaHei UI", 10, "bold"), padx=12, pady=9)
    controls.pack(fill="x", pady=(12, 8))
    command_var = tk.StringVar(value="连接后可控制 LED 和蜂鸣器")
    buttons = []

    def send(command):
        try:
            server.send(command)
            command_var.set(f"已发送：{command}，等待设备应答")
        except (OSError, ValueError) as exc:
            command_var.set(f"发送失败：{exc}")

    for label, command in (("连接测试", "PING"), ("LED 开", "LED,1"),
                           ("LED 关", "LED,0"), ("蜂鸣器开", "BUZZER,1"),
                           ("蜂鸣器关", "BUZZER,0")):
        button = tk.Button(controls, text=label, command=lambda c=command: send(c),
                           font=("Microsoft YaHei UI", 10), bg="white", fg="#17324d",
                           padx=12, pady=5, relief="solid", bd=1, state="disabled")
        button.pack(side="left", padx=(0, 9))
        buttons.append(button)
    tk.Label(body, textvariable=command_var, bg="#edf2f6", fg="#526a7d",
             font=("Microsoft YaHei UI", 9)).pack(anchor="w")

    tk.Label(body, text="位移趋势（最近 60 条）", bg="#edf2f6", fg="#29465d",
             font=("Microsoft YaHei UI", 10, "bold")).pack(anchor="w", pady=(10, 2))
    chart = tk.Canvas(body, height=115, bg="white", highlightthickness=1,
                      highlightbackground="#dce5ec")
    chart.pack(fill="x")
    history = deque(maxlen=60)

    def draw_chart():
        chart.delete("all")
        width = max(chart.winfo_width(), 100)
        height = max(chart.winfo_height(), 100)
        chart.create_line(30, height - 19, width - 10, height - 19, fill="#b7c5d0")
        if len(history) > 1:
            scale = max(20, max(history))
            coords = []
            for index, value in enumerate(history):
                x = 32 + index * (width - 45) / 59
                y = height - 21 - value * (height - 36) / scale
                coords.extend((x, y))
            chart.create_line(*coords, fill="#2384bd", width=2)

    tk.Label(body, text="通信记录", bg="#edf2f6", fg="#29465d",
             font=("Microsoft YaHei UI", 10, "bold")).pack(anchor="w", pady=(10, 2))
    log = tk.Text(body, height=8, state="disabled", bg="white", fg="#34495c",
                  font=("Consolas", 9), wrap="word")
    log.pack(fill="both", expand=True)
    connected = False
    last_data = 0.0

    def write_log(message):
        log.configure(state="normal")
        log.insert("end", f"{time.strftime('%H:%M:%S')}  {message}\n")
        log.see("end")
        log.configure(state="disabled")

    def handle_line(line):
        nonlocal last_data
        packet = parse_telemetry(line)
        if packet is not None:
            mode, sequence, values = packet
            for key, value in values.items():
                metric_vars[key].set(str(value))
            level = evaluate_level(values)
            title, color = LEVELS[level]
            alarm.configure(text=f"等级 {level} · {title}", bg=color)
            source_var.set(f"{'演示' if mode == 'DEMO' else '实测'}数据 · 序号 {sequence} · {time.strftime('%H:%M:%S')}")
            history.append(values["displacement"])
            draw_chart()
            last_data = time.monotonic()
        elif line.startswith("OK,") or line == "PONG":
            command_var.set(f"设备应答：{line}")

    def drain_events():
        nonlocal connected, last_data
        try:
            while True:
                kind, value = server.events.get_nowait()
                if kind == "connected":
                    connected = True
                    last_data = 0.0
                    status_var.set(f"TCP 已连接：{value}，等待固件数据")
                    for button in buttons:
                        button.configure(state="normal")
                elif kind == "disconnected":
                    connected = False
                    last_data = 0.0
                    status_var.set("连接已断开，等待模块重新发现")
                    alarm.configure(text="通信中断", bg="#667a8a")
                    for button in buttons:
                        button.configure(state="disabled")
                elif kind == "discovered":
                    status_var.set(f"已发现模块：{value}，等待 TCP 连接")
                elif kind == "silent":
                    status_var.set("TCP 已连接，但模块 10 秒未发数据；检查固件是否运行")
                    alarm.configure(text="无数据", bg="#667a8a")
                elif kind == "error":
                    status_var.set(value)
                if kind == "line":
                    handle_line(value)
                    if value.startswith("HELLO,"):
                        status_var.set("模块已就绪，等待监测数据")
                    elif parse_telemetry(value) is not None:
                        status_var.set("正在接收监测数据")
                    elif value == "PONG" or value.startswith("OK,"):
                        status_var.set("模块通信正常，等待监测数据")
                write_log(f"{kind}: {value}")
        except queue.Empty:
            pass
        if connected and last_data and time.monotonic() - last_data > 5:
            alarm.configure(text="数据超时", bg="#667a8a")
        root.after(100, drain_events)

    def close():
        server.stop()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    server.start()
    drain_events()
    root.mainloop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=PORT)
    args = parser.parse_args()
    run_dashboard(TestServer(args.bind, args.port))
