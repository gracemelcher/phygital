#!/usr/bin/env python3
"""
USB bridge: reads the ESP32's USB serial output and forwards it to ThingsBoard.

The sketch (with USE_WIFI = false) prints lines like:
    TB:{"r1_state":"moving","r1_dist_m":2.41,...}     -> telemetry
    TBA:{"firmware":"0.2.0-devmodule",...}            -> client attributes
Every other line (the normal status printout) is shown here unchanged,
so this window replaces the Arduino Serial Monitor.

Uses ThingsBoard's HTTP device API over HTTPS (port 443), which works on
most campus networks.

Setup (once):
    python3 -m venv .venv
    source .venv/bin/activate
    pip install pyserial requests

Run (close Arduino's Serial Monitor first: only one program can use the port):
    export TB_TOKEN="your_device_access_token"
    python3 usb_bridge.py --port /dev/cu.usbserial-0001

Stop with Ctrl+C.
"""
import argparse
import json
import os
import sys
import time

try:
    import requests
    import serial
except ImportError:
    sys.exit("Missing libraries. Run:  pip install pyserial requests")


def parse_args():
    p = argparse.ArgumentParser(description="Forward ESP32 USB data to ThingsBoard")
    p.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbserial-0001")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--host", default="thingsboard.cloud",
                   help="ThingsBoard web host (Europe: eu.thingsboard.cloud)")
    p.add_argument("--quiet", action="store_true", help="Hide the normal status lines")
    return p.parse_args()


def open_serial(port, baud):
    """Open the port, retrying if the board is unplugged."""
    while True:
        try:
            s = serial.Serial(port, baud, timeout=1)
            print(f"[bridge] Connected to {port}")
            return s
        except serial.SerialException as e:
            print(f"[bridge] Can't open {port} ({e}). Is the Serial Monitor still open? Retrying in 3 s...")
            time.sleep(3)


def main():
    args = parse_args()
    token = os.environ.get("TB_TOKEN")
    if not token:
        sys.exit('Set your access token first:  export TB_TOKEN="..."')

    base = f"https://{args.host}/api/v1/{token}"
    http = requests.Session()
    sent = errors = 0
    ser = open_serial(args.port, args.baud)

    while True:
        try:
            raw = ser.readline()
        except serial.SerialException:
            print("[bridge] Board disconnected. Waiting for it to come back...")
            ser.close()
            ser = open_serial(args.port, args.baud)
            continue
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()

        if line.startswith("TB:") or line.startswith("TBA:"):
            is_attr = line.startswith("TBA:")
            payload = line[4:] if is_attr else line[3:]
            try:
                json.loads(payload)  # skip garbled lines instead of sending bad JSON
            except ValueError:
                print(f"[bridge] Skipped malformed line: {line[:80]}")
                continue
            url = f"{base}/{'attributes' if is_attr else 'telemetry'}"
            try:
                r = http.post(url, data=payload, headers={"Content-Type": "application/json"}, timeout=5)
                if r.status_code == 200:
                    sent += 1
                    if is_attr or sent % 30 == 1:
                        kind = "attributes" if is_attr else "telemetry"
                        print(f"[bridge] OK: {kind} sent ({sent} messages so far)")
                else:
                    errors += 1
                    hint = " (wrong token or host?)" if r.status_code in (401, 404) else ""
                    print(f"[bridge] ThingsBoard replied {r.status_code}{hint}: {r.text[:120]}")
            except requests.RequestException as e:
                errors += 1
                print(f"[bridge] Network error: {e}")
        elif not args.quiet:
            print(line)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[bridge] Stopped.")
