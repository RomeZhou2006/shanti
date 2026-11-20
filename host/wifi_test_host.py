"""STM32F407 与逐飞 Wi-Fi SPI 模块的 UDP 发现及 TCP 联调上位机。

运行：python wifi_test_host.py             图形窗口
      python wifi_test_host.py --headless  命令行
"""

import argparse
import queue
import socket
import threading
import time
from collections import deque


PORT = 8086
DISCOVERY_PORT = 8087
DISCOVERY_REQUEST = b"DISCOVER,LS1"


def local_ipv4_addresses():
    addresses = set()
    try:
        addresses.update(socket.gethostbyname_ex(socket.gethostname())[2])
    except OSError:
        pass
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("192.0.2.1", 1))
            addresses.add(probe.getsockname()[0])
    except OSError:
        pass
    return sorted(a for a in addresses if not a.startswith("127."))


class TestServer:
    def __init__(self, bind, port):
        self.bind = bind
        self.port = port
        self.events = queue.Queue()
        self.commands = queue.Queue()
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.discovery_thread = threading.Thread(target=self._discover, daemon=True)

    def start(self):
        self.thread.start()
        self.discovery_thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=1.0)
        self.discovery_thread.join(timeout=1.0)

    def _discover(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind((self.bind, DISCOVERY_PORT))
                listener.settimeout(0.25)
                self.events.put(("discovery_listening", f"UDP {DISCOVERY_PORT}"))
                while not self.stop_event.is_set():
                    try:
                        request, sender = listener.recvfrom(128)
                    except socket.timeout:
                        continue
                    if request.strip() != DISCOVERY_REQUEST:
                        continue
                    try:
                        if self.bind == "0.0.0.0":
                            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
                                route.connect(sender)
                                host_ip = route.getsockname()[0]
                        else:
                            host_ip = self.bind
                        reply = f"HOST,{host_ip},{self.port}\n".encode("ascii")
                        listener.sendto(reply, sender)
                        self.events.put(("discovered", f"{sender[0]} → {host_ip}:{self.port}"))
                    except OSError as exc:
                        self.events.put(("error", f"发现应答失败：{exc}"))
        except OSError as exc:
            self.events.put(("error", f"UDP 发现监听失败：{exc}"))

    def send(self, command):
        if command not in ("PING", "LED,1", "LED,0", "BUZZER,1", "BUZZER,0"):
            raise ValueError("Unsupported test command")
        self.commands.put(command + "\n")

    def _run(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind((self.bind, self.port))
                listener.listen(1)
                listener.settimeout(0.25)
                self.events.put(("listening", f"{self.bind}:{listener.getsockname()[1]}"))
                while not self.stop_event.is_set():
                    try:
                        peer, address = listener.accept()
                    except socket.timeout:
                        continue
                    except OSError:
                        break
                    with peer:
                        self.events.put(("connected", f"{address[0]}:{address[1]}"))
                        peer.settimeout(0.2)
                        self._serve_peer(peer)
                        self.events.put(("disconnected", "连接已断开"))
        except OSError as exc:
            self.events.put(("error", f"监听失败：{exc}"))

    def _serve_peer(self, peer):
        pending = bytearray()
        last_received = time.monotonic()
        silence_reported = False
        while not self.stop_event.is_set():
            try:
                while True:
                    command = self.commands.get_nowait()
                    peer.sendall(command.encode("ascii"))
                    self.events.put(("sent", command.strip()))
            except queue.Empty:
                pass
            except OSError:
                return
            try:
                chunk = peer.recv(4096)
            except socket.timeout:
                if not silence_reported and time.monotonic() - last_received >= 10.0:
                    self.events.put(("silent", "TCP 已连接，但 10 秒内没有收到模块数据"))
                    silence_reported = True
                continue
            except OSError:
                return
            if not chunk:
                return
            last_received = time.monotonic()
            silence_reported = False
            pending.extend(chunk)
            if len(pending) > 4096:
                self.events.put(("error", "收到过长数据，已清空缓冲区"))
                pending.clear()
            while b"\n" in pending:
                raw, _, remaining = pending.partition(b"\n")
                pending = bytearray(remaining)
                self.events.put(("line", raw.rstrip(b"\r").decode("utf-8", errors="replace")))


def run_gui(server):
    import tkinter as tk
    from tkinter import ttk

    root = tk.Tk()
    root.title("山体滑坡预警系统 · Wi-Fi SPI 联调上位机")
    root.geometry("760x580")
    frame = ttk.Frame(root, padding=14)
    frame.pack(fill="both", expand=True)
    addresses = local_ipv4_addresses()
    ip_text = "、".join(addresses) if addresses else "未检测到局域网 IPv4"
    ttk.Label(frame, text=f"本机 IPv4：{ip_text}     TCP：{server.port}     UDP 发现：{DISCOVERY_PORT}").pack(anchor="w")
    ttk.Label(frame, text="电脑与 Wi-Fi 模块接入同一热点；模块会自动发现电脑 IP，无需修改固件。", wraplength=720).pack(anchor="w", pady=(3, 10))

    status = tk.StringVar(value="正在启动监听…")
    ttk.Label(frame, textvariable=status).pack(anchor="w")
    buttons = ttk.Frame(frame)
    buttons.pack(anchor="w", pady=10)
    for label, command in (("PING", "PING"), ("LED 开", "LED,1"), ("LED 关", "LED,0")):
        ttk.Button(buttons, text=label, command=lambda c=command: server.send(c)).pack(side="left", padx=(0, 8))

    ttk.Label(frame, text="模拟值曲线（仅用于链路测试，不代表山体传感器数据）").pack(anchor="w")
    canvas = tk.Canvas(frame, width=720, height=170, bg="white", highlightthickness=1, highlightbackground="#bbb")
    canvas.pack(fill="x", pady=(4, 10))
    points = deque(maxlen=60)

    ttk.Label(frame, text="通信记录").pack(anchor="w")
    log = tk.Text(frame, height=15, state="disabled", wrap="word")
    log.pack(fill="both", expand=True)

    def write_log(message):
        log.configure(state="normal")
        log.insert("end", f"{time.strftime('%H:%M:%S')}  {message}\n")
        log.see("end")
        log.configure(state="disabled")

    def draw_curve():
        canvas.delete("all")
        width = max(canvas.winfo_width(), 2)
        height = max(canvas.winfo_height(), 2)
        canvas.create_line(30, 10, 30, height - 20, fill="#999")
        canvas.create_line(30, height - 20, width - 10, height - 20, fill="#999")
        canvas.create_text(20, 10, text="999", anchor="nw", fill="#777")
        canvas.create_text(20, height - 25, text="0", anchor="e", fill="#777")
        if len(points) > 1:
            coords = []
            for index, value in enumerate(points):
                x = 32 + index * (width - 45) / 59
                y = height - 21 - value * (height - 34) / 999
                coords.extend((x, y))
            canvas.create_line(*coords, fill="#1677be", width=2)

    def drain_events():
        try:
            while True:
                kind, value = server.events.get_nowait()
                if kind == "listening":
                    status.set(f"监听中：{value}")
                elif kind == "discovery_listening":
                    status.set(f"等待模块发现：{value}")
                elif kind == "discovered":
                    status.set(f"已发现模块：{value}，等待 TCP 连接")
                elif kind == "connected":
                    status.set(f"已连接：{value}")
                elif kind == "disconnected":
                    status.set("等待模块重新连接")
                elif kind == "error":
                    status.set(value)
                if kind == "line":
                    parts = value.split(",")
                    if len(parts) == 3 and parts[0] == "SIM":
                        try:
                            sample = int(parts[2])
                            if 0 <= sample <= 999:
                                points.append(sample)
                                draw_curve()
                        except ValueError:
                            pass
                write_log(f"{kind}: {value}")
        except queue.Empty:
            pass
        root.after(100, drain_events)

    def close():
        server.stop()
        root.destroy()

    root.protocol("WM_DELETE_WINDOW", close)
    server.start()
    drain_events()
    root.mainloop()


def run_headless(server):
    server.start()
    try:
        while True:
            try:
                kind, value = server.events.get(timeout=0.5)
                print(f"{kind}: {value}", flush=True)
            except queue.Empty:
                pass
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="Listening IPv4 address")
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--headless", action="store_true")
    options = parser.parse_args()
    test_server = TestServer(options.bind, options.port)
    if options.headless:
        run_headless(test_server)
    else:
        run_gui(test_server)
