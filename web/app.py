#!/usr/bin/env python3
"""LED Sign Web UI — Flask app for controlling the sign via the BLE bridge."""

import json
import os
import signal
import subprocess
import sys
import time
import threading
import urllib.request
import urllib.error

from flask import Flask, render_template, request, jsonify

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import sign

app = Flask(__name__)

BRIDGE_URL = os.environ.get("BRIDGE_URL", "http://matts-iphone.local:8080")
SIGN_PY = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sign.py")

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

class SignState:
    def __init__(self):
        self.mode = "off"  # off, clock, text, calendar, headlines, spotify, dadjoke, message
        self.clock_process = None
        self.clock_forecast = "none"
        self.brightness = 10
        self.custom_text = ""
        self.custom_color = "#3282ff"
        self.custom_scroll = "left"
        self.custom_speed = 10
        self.message = ""
        self.message_color = "#ffffff"
        self.lock = threading.Lock()

state = SignState()

# ---------------------------------------------------------------------------
# Bridge helpers
# ---------------------------------------------------------------------------

def bridge_request(path, method="POST"):
    try:
        req = urllib.request.Request(f"{BRIDGE_URL}{path}", method=method)
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())
    except Exception as e:
        return {"error": str(e)}

def bridge_status():
    return bridge_request("/status", method="GET")

# ---------------------------------------------------------------------------
# Clock process management
# ---------------------------------------------------------------------------

def start_clock(forecast="none"):
    stop_clock()
    with state.lock:
        state.clock_forecast = forecast
        state.clock_process = subprocess.Popen(
            [sys.executable, SIGN_PY, "clock", "--forecast", forecast],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )

def stop_clock():
    with state.lock:
        if state.clock_process and state.clock_process.poll() is None:
            state.clock_process.terminate()
            try:
                state.clock_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                state.clock_process.kill()
            state.clock_process = None
    # Also kill any manually-started clock processes
    subprocess.run(["pkill", "-f", "sign.py clock"], capture_output=True)

def send_text(text, color="#3282ff", scroll="left", speed=10):
    r = int(color[1:3], 16)
    g = int(color[3:5], 16)
    b = int(color[5:7], 16)
    segments = [(text, (r, g, b))]
    packets = sign.build_text_program(
        "", font_size=16, scroll=scroll, speed=speed,
        segments=segments, font_family=0x00
    )
    for pkt in packets:
        sign.send_packet(pkt, BRIDGE_URL)
        time.sleep(0.15)

def send_message(text, color="#ffffff"):
    r = int(color[1:3], 16)
    g = int(color[3:5], 16)
    b = int(color[5:7], 16)
    segments = [(text, (r, g, b))]
    packets = sign.build_text_program(
        "", font_size=16, scroll="static", speed=10,
        segments=segments, font_family=0x00
    )
    for pkt in packets:
        sign.send_packet(pkt, BRIDGE_URL)
        time.sleep(0.15)

# ---------------------------------------------------------------------------
# Headlines
# ---------------------------------------------------------------------------

def fetch_filtered_news():
    """Fetch breaking news filtered by sign.py's keyword lists."""
    return sign.fetch_breaking_news()

# ---------------------------------------------------------------------------
# Dad jokes
# ---------------------------------------------------------------------------

def fetch_dad_joke():
    try:
        req = urllib.request.Request(
            "https://icanhazdadjoke.com/",
            headers={"Accept": "application/json", "User-Agent": "LEDSign/1.0"}
        )
        with urllib.request.urlopen(req, timeout=5) as resp:
            return json.loads(resp.read())["joke"]
    except Exception:
        return "Why did the LED sign cross the road? To display the other side."

# ---------------------------------------------------------------------------
# Background mode runner
# ---------------------------------------------------------------------------

_mode_thread = None
_mode_stop = threading.Event()

def _run_headlines_mode():
    while not _mode_stop.is_set():
        headlines = fetch_filtered_news()
        if not headlines:
            _mode_stop.wait(300)
            continue
        for hl in headlines:
            if _mode_stop.is_set():
                return
            send_text(hl, color="#ff6400", scroll="left", speed=8)
            _mode_stop.wait(max(len(hl) * 0.35, 5))

def _run_dadjoke_mode():
    while not _mode_stop.is_set():
        joke = fetch_dad_joke()
        if _mode_stop.is_set():
            return
        send_text(joke, color="#00ff88", scroll="left", speed=8)
        _mode_stop.wait(max(len(joke) * 0.5, 10))

def start_mode_thread(target):
    global _mode_thread
    stop_mode_thread()
    _mode_stop.clear()
    _mode_thread = threading.Thread(target=target, daemon=True)
    _mode_thread.start()

def stop_mode_thread():
    global _mode_thread
    _mode_stop.set()
    if _mode_thread and _mode_thread.is_alive():
        _mode_thread.join(timeout=3)
    _mode_thread = None

# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/status")
def api_status():
    bridge = bridge_status()
    clock_running = state.clock_process and state.clock_process.poll() is None
    return jsonify({
        "mode": state.mode,
        "bridge": bridge,
        "clock_running": clock_running,
        "clock_forecast": state.clock_forecast,
        "brightness": state.brightness,
    })

@app.route("/api/mode", methods=["POST"])
def api_set_mode():
    data = request.get_json(force=True)
    new_mode = data.get("mode", "off")

    stop_clock()
    stop_mode_thread()

    if new_mode == "clock":
        forecast = data.get("forecast", state.clock_forecast)
        start_clock(forecast)
        state.mode = "clock"
        state.clock_forecast = forecast

    elif new_mode == "text":
        text = data.get("text", state.custom_text)
        color = data.get("color", state.custom_color)
        scroll = data.get("scroll", state.custom_scroll)
        speed = data.get("speed", state.custom_speed)
        state.custom_text = text
        state.custom_color = color
        state.custom_scroll = scroll
        state.custom_speed = speed
        state.mode = "text"
        send_text(text, color, scroll, speed)

    elif new_mode == "headlines":
        state.mode = "headlines"
        start_mode_thread(_run_headlines_mode)

    elif new_mode == "dadjoke":
        state.mode = "dadjoke"
        start_mode_thread(_run_dadjoke_mode)

    elif new_mode == "message":
        msg = data.get("text", state.message)
        color = data.get("color", state.message_color)
        state.message = msg
        state.message_color = color
        state.mode = "message"
        send_message(msg, color)

    elif new_mode == "off":
        state.mode = "off"
        bridge_request("/power/off")

    return jsonify({"ok": True, "mode": state.mode})

@app.route("/api/brightness", methods=["POST"])
def api_brightness():
    data = request.get_json(force=True)
    level = max(0, min(15, int(data.get("level", 10))))
    state.brightness = level
    bridge_request(f"/brightness/{level}")
    return jsonify({"ok": True, "brightness": level})

@app.route("/api/power", methods=["POST"])
def api_power():
    data = request.get_json(force=True)
    action = data.get("action", "on")
    bridge_request(f"/power/{action}")
    return jsonify({"ok": True, "power": action})

@app.route("/api/forecast", methods=["POST"])
def api_forecast():
    """Show the forecast graph immediately."""
    weather = sign.fetch_weather(38.895, -77.264)
    hourly = weather.get("hourly", [])
    hourly_uv = weather.get("hourly_uv", [])
    from datetime import datetime
    now = datetime.now()
    fc_time = f"{now.strftime('%-I')}:{now.strftime('%M')}"
    img = sign.render_forecast_fullscreen(
        hourly, now.hour, time_str=fc_time,
        temp_now=weather["temp"], icon_name=weather["icon"],
        hourly_uv=hourly_uv,
    )
    sign.rt_draw_color_image(img, BRIDGE_URL, max_colors=12)
    return jsonify({"ok": True, "action": "forecast"})

@app.route("/api/headlines")
def api_headlines():
    headlines = fetch_filtered_news()
    return jsonify({"headlines": headlines})

@app.route("/api/dadjoke")
def api_dadjoke():
    return jsonify({"joke": fetch_dad_joke()})

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    print("LED Sign Web UI: http://localhost:5050")
    app.run(host="0.0.0.0", port=5050, debug=False)
