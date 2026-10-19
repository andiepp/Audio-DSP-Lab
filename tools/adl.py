#!/usr/bin/env python3
"""Audio-DSP-Lab from Python: a small client for the board's serial protocol.

As a command line tool:

    python adl.py COM20 info "app generator" "set frequency 440" "set output on"
    python adl.py COM20            (interactive: type requests, see events)
    python adl.py --list           (the serial ports)

As a module:

    from adl import Board
    with Board("COM20") as board:
        board.request("app generator")
        board.set("frequency", 440)
        print(board.get("amplitude"))

Needs pyserial (pip install pyserial). The protocol is described in
docs/Protocol.md.
"""

import argparse
import queue
import sys
import threading
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:  # pragma: no cover - a hint instead of a stack trace
    sys.exit("adl.py needs pyserial: pip install pyserial")

BAUD_RATE = 921600
DEFAULT_TIMEOUT = 2.0
APPLICATION_TIMEOUT = 12.0


class BoardError(Exception):
    """The board answered 'err <code> <message>'."""

    def __init__(self, request, code, detail):
        super().__init__(f"{request}: {code} {detail}".strip())
        self.request = request
        self.code = code
        self.detail = detail


def tokenize(line):
    """Splits a protocol line into words; "quoted text" is one word."""
    words, i, n = [], 0, len(line)
    while True:
        while i < n and line[i] in " \t":
            i += 1
        if i >= n:
            return words
        word = []
        if line[i] == '"':
            i += 1
            while i < n and line[i] != '"':
                if line[i] == "\\" and i + 1 < n and line[i + 1] in '"\\':
                    i += 1
                word.append(line[i])
                i += 1
            i += 1
        else:
            while i < n and line[i] not in " \t":
                word.append(line[i])
                i += 1
        words.append("".join(word))


def quote(text):
    """A text as the protocol quotes it."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


class Response:
    """The data lines ('* ...') and the words after 'ok'."""

    def __init__(self, data, values):
        self.data = data
        self.values = values

    @property
    def value(self):
        return self.values[0] if self.values else ""

    def __repr__(self):
        return f"Response(data={self.data!r}, values={self.values!r})"


class Board:
    """One Audio-DSP-Lab board on a serial port.

    Requests run one at a time. Events ('! ...') go to on_event, a function
    taking the event's name and its values; by default they are kept in
    the queue 'events'.
    """

    def __init__(self, port, on_event=None):
        self.port = serial.Serial()
        self.port.port = port
        self.port.baudrate = BAUD_RATE
        self.port.timeout = 0.1
        self.port.dtr = False          # DTR and RTS drive the ESP32's reset:
        self.port.rts = False          # set them before opening the port
        self.port.open()
        self.events = queue.Queue()
        self.on_event = on_event or (lambda name, values: self.events.put((name, values)))
        self._lines = queue.Queue()
        self._running = True
        self._lock = threading.Lock()
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()
        self._resync_word = 0
        self.info = self.request("info").values

    def __enter__(self):
        return self

    def __exit__(self, *exception):
        self.close()

    def close(self):
        self._running = False
        self._reader.join(1)
        self.port.close()

    def _read(self):
        buffer = b""
        while self._running:
            try:
                chunk = self.port.read(4096)
            except serial.SerialException:
                return
            if not chunk:
                continue
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("utf-8", errors="replace").rstrip("\r")
                if line.startswith("! "):
                    words = tokenize(line[2:])
                    if words:
                        self.on_event(words[0], words[1:])
                elif line == "ok" or line.startswith(("ok ", "err ", "* ")):
                    self._lines.put(line)

    def request(self, line, timeout=None):
        """Sends one request and returns its Response; raises BoardError on 'err'."""
        if timeout is None:
            timeout = APPLICATION_TIMEOUT if line.split()[:1] in (["app"], ["service"]) else DEFAULT_TIMEOUT
        with self._lock:
            while not self._lines.empty():
                self._lines.get_nowait()
            self.port.write((line + "\n").encode("utf-8"))
            data = []
            deadline = time.monotonic() + timeout
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    self._resynchronize()
                    raise TimeoutError(f"no answer to '{line}' within {timeout} s")
                try:
                    answer = self._lines.get(timeout=remaining)
                except queue.Empty:
                    continue
                if answer.startswith("* "):
                    data.append(tokenize(answer[2:]))
                elif answer.startswith("err "):
                    words = answer.split(" ", 2)
                    raise BoardError(line, words[1], words[2] if len(words) > 2 else "")
                else:
                    return Response(data, tokenize(answer)[1:])

    def _resynchronize(self):
        """After a timeout: send 'echo <word>' and drop everything until it comes back."""
        self._resync_word += 1
        word = f"sync{self._resync_word}"
        self.port.write(f"echo {word}\n".encode("utf-8"))
        deadline = time.monotonic() + DEFAULT_TIMEOUT
        while time.monotonic() < deadline:
            try:
                if self._lines.get(timeout=0.1) == f"ok {word}":
                    return
            except queue.Empty:
                pass

    # --- convenience ----------------------------------------------------------

    def get(self, name):
        return self.request(f"get {name}").value

    def set(self, name, value):
        if isinstance(value, bool):
            value = "on" if value else "off"
        return self.request(f"set {name} {value}").value

    def select(self, application):
        return self.request(f"app {application}").values

    def params(self):
        """The running application's parameters as dictionaries."""
        keys = ("name", "kind", "value", "min", "max", "default", "unit", "choices", "pot")
        return [dict(zip(keys, words)) for words in self.request("params").data]

    def sweep(self, start=None, stop=None, points_per_decade=None, timeout=120):
        """Runs a sweep of the analysis application's Frequency Response and returns its points."""
        for name, value in (("start", start), ("stop", stop), ("points-per-decade", points_per_decade)):
            if value is not None:
                self.set(name, value)
        self.request("sweep start")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.request("sweep").values[0]
            if state != "running":
                break
            time.sleep(0.25)
        keys = ("index", "hz", "left", "right", "rel_left", "rel_right")
        return [dict(zip(keys, words)) for words in self.request("points").data]


def show(words):
    return " ".join(quote(word) if (not word or any(c in word for c in ' "\\')) else word for word in words)


def interactive(board):
    print(f"Connected: {' '.join(board.info)}. Type requests ('help' lists them), an empty line or Ctrl+C ends.")
    board.on_event = lambda name, values: print(f"! {name} {show(values)}".rstrip())
    board.request("events on")
    while True:
        try:
            line = input("> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not line:
            return
        run(board, line)


def run(board, line):
    try:
        response = board.request(line)
        for data in response.data:
            print("* " + show(data))
        print(("ok " + show(response.values)).rstrip())
        return True
    except BoardError as error:
        print(f"err {error.code} {error.detail}".rstrip())
    except TimeoutError as error:
        print(f"(no answer: {error})")
    return False


def main():
    parser = argparse.ArgumentParser(description="Talk to an Audio-DSP-Lab board.")
    parser.add_argument("port", nargs="?", help="the board's serial port, e.g. COM20 or /dev/ttyUSB0")
    parser.add_argument("requests", nargs="*", help="requests to send, each one quoted")
    parser.add_argument("--list", action="store_true", help="list the serial ports")
    arguments = parser.parse_args()

    if arguments.list or not arguments.port:
        for port in serial.tools.list_ports.comports():
            print(f"{port.device:12} {port.description}")
        return 0

    with Board(arguments.port) as board:
        if not arguments.requests:
            interactive(board)
            return 0
        ok = all([run(board, line) for line in arguments.requests])
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
