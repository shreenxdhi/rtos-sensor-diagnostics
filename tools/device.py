from collections import deque
from pathlib import Path
import json
import socket
import subprocess
import tempfile
import threading
import time

from protocol import Frame, COMMAND, STATUS

class Device:
    def __init__(self, elf="build/firmware.elf", qemu="qemu-system-arm"):
        self.elf, self.qemu = str(Path(elf).resolve()), qemu
        self.events = deque(maxlen=4096)
        self.condition = threading.Condition()
        self.counter = 0
        self.token = 0
        self.errors = []
        self.closed = False

    def __enter__(self):

        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.stderr = tempfile.TemporaryFile()
        self.process = subprocess.Popen([
            self.qemu, "-M", "mps2-an385", "-display", "none", "-monitor", "none",
            "-serial", f"tcp:127.0.0.1:{port},server=on,wait=on",
            "-kernel", self.elf,
        ], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=self.stderr)
        deadline = time.monotonic() + 8
        try:
            while True:
                try:
                    self.socket = socket.create_connection(("127.0.0.1", port), timeout=0.1)
                    self.socket.settimeout(None)
                    break
                except OSError:
                    if self.process.poll() is not None or time.monotonic() >= deadline:
                        self.stderr.seek(0)
                        raise RuntimeError("QEMU did not start: " + self.stderr.read().decode(errors="replace"))
                    time.sleep(0.02)
            self.thread = threading.Thread(target=self._read, daemon=True)
            self.thread.start()
            self.wait(lambda e: e.get("event") == "boot", timeout=8)
            return self
        except BaseException:
            self.close()
            raise

    def _read(self):
        try:
            with self.socket.makefile("rb") as stream:
                while not self.closed:
                    line = stream.readline(4097)
                    if not line:
                        break
                    try:
                        event = json.loads(line)
                    except (ValueError, UnicodeDecodeError):
                        with self.condition:
                            self.errors.append(repr(line))
                            self.condition.notify_all()
                        continue
                    with self.condition:
                        self.counter += 1
                        self.events.append((self.counter, event))
                        self.condition.notify_all()
        except OSError:
            pass
        finally:
            with self.condition:
                self.condition.notify_all()

    def mark(self):
        with self.condition:
            return self.counter

    def wait(self, predicate, timeout=4, since=0):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                for index, event in self.events:
                    if event.get("event") == "fatal":
                        raise RuntimeError(f"firmware fatal: {event}")
                    if index > since and predicate(event):
                        return event
                if self.errors:
                    raise RuntimeError(f"invalid firmware output: {self.errors[-1]}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    recent = list(self.events)[-3:]
                    raise TimeoutError(f"condition not met; recent events: {recent}")
                self.condition.wait(min(remaining, 0.1))

    def send(self, frame):
        self.raw(frame.encode())

    def raw(self, data):
        self.socket.sendall(data)

    def command(self, opcode, value=0, ok=True):
        self.token = self.token % 65535 + 1
        token = self.token
        mark = self.mark()
        self.send(Frame(COMMAND, opcode, token, value))
        event = self.wait(lambda e: e.get("event") == "ack" and e.get("token") == token, since=mark)
        if event["ok"] != int(ok):
            raise AssertionError(f"unexpected ACK: {event}")
        return token

    def status(self):
        token = self.command(STATUS)
        return self.wait(lambda e: e.get("event") == "status" and e.get("token") == token)

    def until_status(self, predicate, timeout=4, since=0):
        return self.wait(lambda e: e.get("event") == "status" and predicate(e), timeout, since)

    def close(self):
        self.closed = True
        if hasattr(self, "socket"):
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.socket.close()
        if hasattr(self, "process"):
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
        if hasattr(self, "thread"):
            self.thread.join(timeout=2)
        if hasattr(self, "stderr"):
            self.stderr.close()

    def __exit__(self, *_):
        self.close()
