#!/usr/bin/env python3
"""Capture bytes received by a real terminal, without executing pasted text."""
import os
import sys
import termios
import tty
from pathlib import Path

path = Path(sys.argv[1])
previous = termios.tcgetattr(0)
try:
    tty.setraw(0)
    os.write(1, "Hyprvoice · 终端粘贴验收\r\n".encode())
    data = b""
    while True:
        chunk = os.read(0, 4096)
        if not chunk:
            break
        data += chunk
        path.write_bytes(data)
        os.write(1, chunk)
finally:
    termios.tcsetattr(0, termios.TCSANOW, previous)
