#!/usr/bin/env python3
"""
hatari_run.py -- boot a floppy image in Hatari without a window and script it.

USAGE
    python3 tools/hatari_run.py [options] DISK_A [STEP ...]

    DISK_A              floppy image for drive A
    STEP                one of:
        wait:SECONDS        let the emulator run
        shot:NAME           save a screenshot as OUTDIR/NAME.png
        key:KEY             press and release an ST key (Hatari key name or
                            scancode, e.g. key:57 for space, key:28 return)
        disk:FILE           insert FILE into drive A (swap floppies)
        type:TEXT           type TEXT (letters, digits, space, '/', '.', '-')

    --tos FILE          TOS ROM (default ~/opt/emutos/emutos-512k-1.4/etos512us.img)
    --outdir DIR        screenshot directory (default build/hatari)
    --memsize KIB       ST RAM in KiB (default 1024)
    --window            show the Hatari window instead of running headless

The emulator is an ST with a monochrome monitor and fast floppy access,
controlled through Hatari's --control-socket. It is quit after the last
step. See docs/hatari.md.
"""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

# ST scancodes for type: (unshifted US layout).
SCANCODES = {
    "1": 2, "2": 3, "3": 4, "4": 5, "5": 6, "6": 7, "7": 8, "8": 9, "9": 10,
    "0": 11, "-": 12, "q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21,
    "u": 22, "i": 23, "o": 24, "p": 25, "a": 30, "s": 31, "d": 32, "f": 33,
    "g": 34, "h": 35, "j": 36, "k": 37, "l": 38, "z": 44, "x": 45, "c": 46,
    "v": 47, "b": 48, "n": 49, "m": 50, ".": 52, "/": 53, " ": 57,
}


def main():
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("-h", "--help", action="store_true")
    ap.add_argument("--tos", default=os.path.expanduser(
        "~/opt/emutos/emutos-512k-1.4/etos512us.img"))
    ap.add_argument("--outdir", default="build/hatari")
    ap.add_argument("--memsize", default="1024")
    ap.add_argument("--window", action="store_true")
    ap.add_argument("disk", nargs="?")
    ap.add_argument("steps", nargs="*")
    args = ap.parse_args()
    if args.help or not args.disk:
        sys.stderr.write(__doc__)
        sys.exit(2)

    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="hatari")
    sock_path = os.path.join(tmp, "control")
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(sock_path)
    server.listen(1)

    env = dict(os.environ)
    if not args.window:
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
    cmd = ["hatari", "--confirm-quit", "false", "--machine", "st",
           "--monitor", "mono", "--tos", args.tos, "--memsize", args.memsize,
           "--fastfdc", "true", "--sound", "off", "--statusbar", "false",
           "--screenshot-dir", outdir, "--screenshot-format", "png",
           "--control-socket", sock_path, "--disk-a", os.path.abspath(args.disk)]
    log = open(os.path.join(outdir, "hatari.log"), "w")
    proc = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)

    server.settimeout(30)
    conn, _ = server.accept()

    def send(line):
        conn.sendall((line + "\n").encode())
        time.sleep(0.2)

    def screenshot(name):
        before = set(os.listdir(outdir))
        send("hatari-shortcut screenshot")
        for _ in range(50):
            new = [f for f in set(os.listdir(outdir)) - before if f.endswith(".png")]
            if new:
                time.sleep(0.3)
                os.replace(os.path.join(outdir, new[0]),
                           os.path.join(outdir, name + ".png"))
                print("screenshot %s/%s.png" % (outdir, name))
                return
            time.sleep(0.1)
        print("screenshot %s: none produced" % name)

    try:
        for step in args.steps:
            kind, _, value = step.partition(":")
            if kind == "wait":
                time.sleep(float(value))
            elif kind == "shot":
                screenshot(value)
            elif kind == "key":
                send("hatari-event keypress %s" % value)
            elif kind == "disk":
                send("hatari-option --disk-a %s" % os.path.abspath(value))
            elif kind == "type":
                for ch in value:
                    send("hatari-event keypress %d" % SCANCODES[ch.lower()])
            else:
                sys.exit("unknown step %s" % step)
            if proc.poll() is not None:
                print("hatari exited early, see %s/hatari.log" % outdir)
                break
    finally:
        try:
            send("hatari-shortcut quit")
        except OSError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
        conn.close()
        server.close()
        os.unlink(sock_path)
        os.rmdir(tmp)


if __name__ == "__main__":
    main()
