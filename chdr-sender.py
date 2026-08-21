#!/usr/bin/env python3
"""
chdr_send.py — Send a pre-packed CHDR binary over serial.
Usage:
    python3 chdr_send.py --port /dev/ttyUSB0 --baud 115200 --file payload.chdr
"""
import argparse
import serial
import sys

CHUNK_SIZE = 256  # bytes per write chunk. Adjust to your loader's expectations

def send_with_progress(ser, data, address):
    total = len(data)
    sent = 0
    bar_width = 40

    print(f"Writing to address {address:#010x}...", end="", flush=True)

    while sent < total:
        chunk = data[sent:sent + CHUNK_SIZE]
        ser.write(chunk)
        ser.flush()
        sent += len(chunk)

        pct = sent / total
        filled = int(bar_width * pct)
        bar = "█" * filled + "░" * (bar_width - filled)

        print(f"\rWriting to address {address:#010x}... [{bar}] {pct*100:5.1f}%  {sent}/{total} bytes",
              end="", flush=True)

    print()  # newline after completion


def main():
    parser = argparse.ArgumentParser(description="Send a pre-packed CHDR binary over serial")
    parser.add_argument("--port",    "-p", required=True,          help="Serial port, e.g. /dev/ttyUSB0")
    parser.add_argument("--baud",    "-b", default=115200, type=int)
    parser.add_argument("--file",    "-f", required=True,          help="Pre-packed CHDR binary file")
    parser.add_argument("--address", "-a", default="0x01080000",   help="Target load address (hex)")
    args = parser.parse_args()

    address = int(args.address, 16)

    with open(args.file, "rb") as f:
        data = f.read()

    print(f"File         : {args.file}")
    print(f"Total size   : {len(data)} bytes ({len(data):#010x})")
    print(f"Target addr  : {address:#010x}")
    print(f"First 16 bytes (header): {data[:16].hex(' ')}")
    print()

    with serial.Serial(args.port, args.baud, timeout=30) as ser:
        send_with_progress(ser, data, address)

        print("Sent. Waiting for loader response...")
        while True:
            line = ser.readline()
            if not line:
                break
            print(line.decode(errors="replace"), end="")

    print("\nDone.")

if __name__ == "__main__":
    main()
