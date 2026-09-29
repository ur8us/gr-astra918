"""Optional framed CAT access to the independent USB CDC serial port."""

import os
import queue
import threading
import time

from gnuradio import gr
import pmt


def _command_bytes(message):
    if pmt.is_symbol(message):
        raw = pmt.symbol_to_string(message).encode("ascii")
    elif pmt.is_u8vector(message):
        raw = bytes(pmt.u8vector_elements(message))
    elif pmt.is_pair(message) and pmt.is_u8vector(pmt.cdr(message)):
        raw = bytes(pmt.u8vector_elements(pmt.cdr(message)))
    else:
        raise ValueError("CAT command must be a text symbol or byte-vector PDU")
    if not raw.endswith(b";"):
        raw += b";"
    if len(raw) > 32 or raw.count(b";") != 1 or any(c < 32 or c > 126 for c in raw):
        raise ValueError("CAT command must be one printable ASCII request of at most 32 bytes")
    return raw


class CatSerial(gr.basic_block):
    """CAT responses as text and raw byte PDUs; optional FA polling.

    Leave port blank when WSJT-X owns CAT. A serial port cannot normally have
    two independent owners; decoded tuning state remains available on the
    Astra918 I/Q Source status output without opening this block.
    """

    def __init__(self, port="", baud=115200, poll_interval=1.0):
        gr.basic_block.__init__(self, name="Astra918 CAT serial", in_sig=None, out_sig=None)
        self.port = port
        self.baud = int(baud)
        self.poll_interval = float(poll_interval)
        if self.baud <= 0 or self.poll_interval < 0:
            raise ValueError("CAT baud must be positive and poll interval nonnegative")
        self._commands = queue.Queue(maxsize=32)
        self._stop = threading.Event()
        self._serial = None
        self._thread = None
        self._cmd_port = pmt.intern("cmd")
        self._raw_port = pmt.intern("bytes")
        self._text_port = pmt.intern("text")
        self._status_port = pmt.intern("status")
        self.message_port_register_in(self._cmd_port)
        for port_id in (self._raw_port, self._text_port, self._status_port):
            self.message_port_register_out(port_id)
        self.set_msg_handler(self._cmd_port, self._enqueue)

    def _status(self, **values):
        self.message_port_pub(self._status_port, pmt.to_pmt(values))

    def _enqueue(self, message):
        try:
            self._commands.put_nowait(_command_bytes(message))
        except (ValueError, UnicodeEncodeError, queue.Full) as exc:
            self._status(connected=self._serial is not None, error=str(exc))

    def start(self):
        if not self.port:
            self._status(connected=False, detail="CAT port blank; USB serial left to another program")
            return True
        try:
            import serial

            options = {"exclusive": True} if os.name == "posix" else {}
            self._serial = serial.Serial(
                self.port, self.baud, timeout=0.1, write_timeout=0.5, **options
            )
        except Exception as exc:
            self._status(connected=False, port=self.port, error=str(exc))
            raise
        self._stop.clear()
        self._thread = threading.Thread(target=self._read_loop, name="Astra918 CAT", daemon=True)
        self._thread.start()
        self._status(connected=True, port=self.port)
        return True

    def _publish_reply(self, raw):
        self.message_port_pub(
            self._raw_port, pmt.cons(pmt.make_dict(), pmt.init_u8vector(len(raw), list(raw)))
        )
        text = raw.decode("ascii", errors="replace")
        self.message_port_pub(self._text_port, pmt.intern(text))
        if text.startswith("FA") and text.endswith(";") and text[2:-1].isdigit():
            self._status(connected=True, dial_hz=int(text[2:-1]))

    def _read_loop(self):
        pending = bytearray()
        next_poll = time.monotonic()
        try:
            while not self._stop.is_set():
                now = time.monotonic()
                if self.poll_interval and now >= next_poll:
                    self._serial.write(b"FA;")
                    next_poll = now + self.poll_interval
                try:
                    command = self._commands.get_nowait()
                except queue.Empty:
                    pass
                else:
                    self._serial.write(command)
                data = self._serial.read(64)
                if data:
                    pending.extend(data)
                    if len(pending) > 256:
                        pending.clear()
                        self._status(connected=True, error="CAT reply exceeded 256 bytes")
                    while b";" in pending:
                        end = pending.index(b";") + 1
                        self._publish_reply(bytes(pending[:end]))
                        del pending[:end]
        except Exception as exc:
            if not self._stop.is_set():
                self._status(connected=False, port=self.port, error=str(exc))

    def stop(self):
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=1)
            self._thread = None
        if self._serial is not None:
            self._serial.close()
            self._serial = None
        return True
