#!/usr/bin/env python3
"""
lisaem_run.py -- boot a ProFile image in LisaEm and capture serial port B.

USAGE
    python3 tools/lisaem_run.py [options] IMAGE

    IMAGE           raw ProFile image for the built-in parallel port

    --lisaem APP    LisaEm.app to run (default ~/github/lisaem-minix/bin/LisaEm.app)
    --rom FILE      boot ROM (default /Applications/LisaEm Files/H ROM/boot.ROM)
    --mem KB        RAM: 512, 1024, 1536 or 2048 (default 1024)
    --timeout SEC   stop after this many seconds (default 90)
    --until REGEX   stop as soon as the serial output matches REGEX
    --log FILE      serial log with elapsed times (default build/lisaem/serial.log)
    --send RE=TEXT  when the output (since the previous step) matches RE, send
                    TEXT followed by a carriage return; repeatable, done in
                    order; --until is only checked after the last step.
                    A TEXT ending in \\c is sent without the carriage return.
                    Characters go one at a time, --char-delay seconds apart
                    (default 0.3), after waiting --send-wait seconds (default
                    2) from the match: LisaEm limits the speed of serial input
    --linger SEC    keep LisaEm running this long after --until matched, e.g.
                    so the screen dump catches up (default 0)
    --keyboard      type the --send text on the Lisa keyboard instead of the
                    serial port: characters are appended to
                    build/lisaem/keyboard.txt, which LisaEm
                    (LISAEM_KEYBOARD_FILE) types through the COPS; the
                    carriage return becomes the Return key
    --env NAME=VAL  extra environment variable for LisaEm (repeatable), e.g.
                    LISAEM_CPU_TRACE=build/lisaem/cpu.trace

The image is attached as the ProFile on the parallel port and serial port B
is LisaEm's TelnetD backend on 127.0.0.1, which this script reads as a raw
TCP stream (a PseudoTTY's line discipline mangled or dropped output).
LisaEm only creates the listener at the first SCC access from outside the
boot ROM, and drops output while no client is connected, so the kernel
should pause briefly after setting up the SCC. LisaEm is powered on with -p and
killed when the run ends; the exit status is 0 if --until matched (or if no
--until was given), 1 otherwise.

Isolation from a LisaEm the user may be running (see docs/lisaem.md):
  - LisaEm is started from a copy of its executable named lisaem-minix, so
    its wx preferences file is "lisaem-minix Preferences", not the user's;
  - the machine configuration is a file in build/lisaem passed with -c,
    with the parameter RAM from tools/lisaem-pram.txt so the Lisa starts up
    from the ProFile instead of showing the STARTUP FROM menu;
  - before the run the user's preferences files are hashed and afterwards
    checked; the run fails loudly if any changed.
LISAEM_NO_DIALOGS=1 is set, which LisaEm (3020b09 or later) needs to
log message boxes to stderr (build/lisaem/lisaem.log) instead of waiting
for someone to click them, and LISAEM_SCREEN_DUMP, which makes it save the
Lisa's display to build/lisaem/screen.png about once a second.
LisaEm must not have IMAGE open in another instance.
"""

import argparse
import glob
import hashlib
import os
import re
import select
import socket
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)

CONFIG = """keyboardid=bf2f
serialnumber=ff000000000000ff0000000000000000
cheatromtests=1
doublesided=0
hle=0
4mbmacworks=0
ioromver=a8
MemoryKB={mem}
no_warn_xl_rom=0
consoletermwindow=0
ROMFILE={rom}
DUALPARALLELROM=
[imagewriter]
saveaspng=0
[parallelport]
parallelport=ProFile
path={image}
[seriala]
connecta=NOTHING
xon=0
parama=
[serialb]
connectb=TelnetD
xon=0
paramb={port}
[cardslot1]
slot1=Nothing
[cardslot2]
slot2=Nothing
[cardslot3]
slot3=Nothing
"""


def user_prefs():
    files = glob.glob(os.path.expanduser("~/Library/Preferences/lisaem*"))
    files = [f for f in files if "lisaem-minix" not in f]
    out = {}
    for f in files:
        if os.path.isfile(f):
            with open(f, "rb") as fh:
                out[f] = hashlib.sha256(fh.read()).hexdigest()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--lisaem", default=os.path.expanduser(
        "~/github/lisaem-minix/bin/LisaEm.app"))
    ap.add_argument("--rom", default="/Applications/LisaEm Files/H ROM/boot.ROM")
    ap.add_argument("--mem", type=int, default=1024, choices=[512, 1024, 1536, 2048])
    ap.add_argument("--timeout", type=float, default=90)
    ap.add_argument("--until")
    ap.add_argument("--log", default=os.path.join(REPO, "build/lisaem/serial.log"))
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--send", action="append", default=[])
    ap.add_argument("--char-delay", type=float, default=0.3)
    ap.add_argument("--send-wait", type=float, default=2.0)
    ap.add_argument("--keyboard", action="store_true")
    ap.add_argument("--linger", type=float, default=0)
    args = ap.parse_args()

    image = os.path.abspath(args.image)
    if not os.path.isfile(image):
        sys.exit("no image %s" % image)
    busy = subprocess.run(["lsof", image], capture_output=True, text=True).stdout
    if busy.strip():
        sys.exit("image is open in another process:\n" + busy)

    workdir = os.path.join(REPO, "build/lisaem")
    os.makedirs(workdir, exist_ok=True)
    macos = os.path.join(args.lisaem, "Contents/MacOS")
    originals = [f for f in os.listdir(macos) if f.startswith("lisaem-arm64")]
    if not originals:
        sys.exit("no lisaem executable in %s" % macos)
    exe = os.path.join(macos, "lisaem-minix")
    src = os.path.join(macos, originals[0])
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        shutil.copy2(src, exe)

    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    conf = os.path.join(workdir, "lisaem-minix.conf")
    with open(os.path.join(HERE, "lisaem-pram.txt")) as f:
        pram = [l for l in f.read().split("\n") if l.startswith("pram")]
    with open(conf, "w") as f:
        f.write(CONFIG.format(mem=args.mem, rom=args.rom, image=image, port=port))
        f.write("[pram]\n" + "\n".join(pram) + "\n")

    prefs_before = user_prefs()
    lisaem_log = open(os.path.join(workdir, "lisaem.log"), "w")
    env = dict(os.environ, LISAEM_NO_DIALOGS="1",
               LISAEM_SCREEN_DUMP=os.path.join(workdir, "screen.png"))
    keyfile = os.path.join(workdir, "keyboard.txt")
    if args.keyboard:
        open(keyfile, "w").close()
        env["LISAEM_KEYBOARD_FILE"] = keyfile
    for item in args.env:
        name, _, value = item.partition("=")
        env[name] = value
    proc = subprocess.Popen([exe, "-c", conf, "-p", "-d"], cwd=workdir, env=env,
                            stdout=lisaem_log, stderr=subprocess.STDOUT,
                            stdin=subprocess.DEVNULL)
    start = time.time()
    serial = open(args.log, "w")
    text = ""
    matched = args.until is None
    sock = None
    try:
        while time.time() - start < args.timeout:
            if proc.poll() is not None:
                sys.exit("LisaEm exited early, see %s/lisaem.log" % workdir)
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                sock = None
                time.sleep(0.01)
        if sock is None:
            sys.exit("no serial listener on port %d after %.0f s: LisaEm creates "
                     "it at the first SCC access from outside the boot ROM; see "
                     "build/lisaem/screen.png" % (port, args.timeout))
        sock.setblocking(False)
        print("LisaEm pid %d, serial B on 127.0.0.1:%d" % (proc.pid, port))
        line = ""
        iac = 0
        steps = [tuple(x.split("=", 1)) for x in args.send]
        step_from = 0
        while time.time() - start < args.timeout:
            if proc.poll() is not None:
                print("LisaEm exited")
                break
            r, _, _ = select.select([sock], [], [], 0.5)
            if not r:
                continue
            try:
                data = sock.recv(4096)
            except BlockingIOError:
                continue
            if not data:
                break
            chunk = ""
            for b in data:
                if iac:                 # skip telnet command bytes
                    iac -= 1
                    continue
                if b == 0xFF:
                    iac = 2
                    continue
                chunk += chr(b)
            chunk = chunk.replace("\r", "")
            text += chunk
            for ch in chunk:
                if ch == "\n":
                    stamped = "%7.2f %s" % (time.time() - start, line)
                    serial.write(stamped + "\n")
                    serial.flush()
                    print(stamped)
                    line = ""
                else:
                    line += ch
            while steps and re.search(steps[0][0], text[step_from:]):
                pattern, reply = steps.pop(0)
                time.sleep(args.send_wait)
                end = b"\r"
                if reply.endswith("\\c"):
                    reply, end = reply[:-2], b""
                for ch in os.fsencode(reply) + end:   # the bytes as given
                    if args.keyboard:
                        with open(keyfile, "ab") as kf:
                            kf.write(b"\n" if ch == 13 else bytes([ch]))
                    else:
                        sock.sendall(bytes([ch]))
                    time.sleep(args.char_delay)
                step_from = len(text)
            if not steps and args.until and re.search(args.until, text[step_from:]):
                matched = True
                time.sleep(args.linger)
                break
        if line:
            serial.write("%7.2f %s\n" % (time.time() - start, line))
            print("%7.2f %s" % (time.time() - start, line))
    finally:
        if sock is not None:
            sock.close()
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        serial.close()
        changed = [f for f, h in user_prefs().items() if prefs_before.get(f) != h]
        changed += [f for f in prefs_before if not os.path.exists(f)]
        if changed:
            print("WARNING: user LisaEm preferences changed during the run: %s" % changed)

    if not matched:
        print("did not match %r within %.0f s" % (args.until, args.timeout))
        sys.exit(1)


if __name__ == "__main__":
    main()
