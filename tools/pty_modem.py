#!/usr/bin/env python3
"""
pty_modem.py — minimal AT-modem stub for debugging 1985's raw serial backend.

1985 with ext_serial_backend=pty prints `serial: PTY ready at /dev/pts/N`
to stderr at startup. Run this script pointing at that path and it will:

  * echo every byte the PCW sends back (so VDU shows what you type),
  * watch for a CR-terminated line that contains "ATDT",
  * reply with a CONNECT banner so VDU enters online mode.

It is intentionally dumb — just enough to exercise the SIO RX path
without PerryFi getting in the way.

Usage:
    ./tools/pty_modem.py /dev/pts/N            # echo + banner on ATDT
    ./tools/pty_modem.py /dev/pts/N -v         # also print byte stream
    ./tools/pty_modem.py /dev/pts/N --no-echo  # don't echo TX back as RX
"""

import argparse
import os
import sys
import termios
import threading
import time


BANNER = (
    b"\r\n"
    b"CONNECT 9600\r\n"
    b"\r\n"
    b"*** pty_modem stub -- connected ***\r\n"
    b"\r\n"
    b"You may type freely; everything is echoed.\r\n"
    b"Press '+' three times then Enter to disconnect (no-op stub).\r\n"
    b"\r\n"
    b"> "
)


def open_pts(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tio = termios.tcgetattr(fd)
    # Raw: no canonical, no echo, no signals.
    tio[0] = 0                              # iflag
    tio[1] = 0                              # oflag
    tio[3] = 0                              # lflag
    tio[6][termios.VMIN] = 0                # non-blocking reads via VMIN/VTIME
    tio[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, tio)
    return fd


def printable(b: int) -> str:
    return chr(b) if 0x20 <= b < 0x7F else "."


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("pts", help="PCW PTY slave path, e.g. /dev/pts/5")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="dump every byte seen / sent")
    ap.add_argument("--no-echo", action="store_true",
                    help="do not echo PCW->host bytes back as host->PCW")
    ap.add_argument("--wake-ms", type=int, default=0,
                    help="send a NUL byte every N ms as a wake-up heartbeat "
                         "(0 = off; try 500 to test if VDU is wedged on an "
                         "empty SIO RX queue)")
    args = ap.parse_args()

    try:
        fd = open_pts(args.pts)
    except OSError as e:
        print(f"open {args.pts}: {e}", file=sys.stderr)
        return 1

    print(f"pty_modem: attached to {args.pts}", file=sys.stderr)
    print("pty_modem: waiting for ATDT...", file=sys.stderr)

    stop = threading.Event()
    if args.wake_ms > 0:
        def heartbeat():
            while not stop.is_set():
                try:
                    os.write(fd, b"\x00")
                except OSError:
                    return
                if args.verbose:
                    sys.stderr.write("TX 00 (heartbeat)\n")
                    sys.stderr.flush()
                stop.wait(args.wake_ms / 1000.0)
        t = threading.Thread(target=heartbeat, daemon=True)
        t.start()
        print(f"pty_modem: heartbeat every {args.wake_ms} ms", file=sys.stderr)

    line = bytearray()

    try:
        while True:
            try:
                chunk = os.read(fd, 256)
            except BlockingIOError:
                chunk = b""

            if not chunk:
                time.sleep(0.01)
                continue

            for b in chunk:
                if args.verbose:
                    sys.stderr.write(f"RX {b:02X} '{printable(b)}'\n")
                    sys.stderr.flush()

                # Echo back so VDU shows what was typed.
                if not args.no_echo:
                    os.write(fd, bytes([b]))

                line.append(b)
                # Look for CR or LF as line terminator.
                if b in (0x0D, 0x0A):
                    text = bytes(line).upper()
                    line.clear()
                    if b"ATDT" in text:
                        print(f"pty_modem: ATDT detected ({text!r}) -- sending banner",
                              file=sys.stderr)
                        os.write(fd, BANNER)
                    elif b"AT" in text:
                        # respond OK to bare AT probes so picky terminals are happy
                        os.write(fd, b"\r\nOK\r\n")
    except KeyboardInterrupt:
        print("\npty_modem: bye", file=sys.stderr)
        return 0
    finally:
        stop.set()
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
