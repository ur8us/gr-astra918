"""Offline checks for CAT framing and GNU Radio message delivery."""

import os
import select
import threading
import time
import unittest

from gnuradio import astra918, blocks, gr
from gnuradio.astra918.cat_serial import _command_bytes
import pmt


class CatTests(unittest.TestCase):
    def test_command_validation(self):
        self.assertEqual(_command_bytes(pmt.intern("FA")), b"FA;")
        self.assertEqual(_command_bytes(pmt.init_u8vector(3, [70, 65, 59])), b"FA;")
        for invalid in ("FA;FB;", "x" * 33, "é"):
            with self.assertRaises((ValueError, UnicodeEncodeError)):
                _command_bytes(pmt.intern(invalid))

    @unittest.skipUnless(os.name == "posix", "pseudo-terminals require POSIX")
    def test_serial_replies_are_text_and_byte_pdus(self):
        master, slave = os.openpty()
        done = threading.Event()
        commands = []

        def receiver():
            while not done.is_set():
                readable, _, _ = select.select([master], [], [], 0.1)
                if readable:
                    command = os.read(master, 128)
                    commands.append(command)
                    if b"FA;" in command:
                        os.write(master, b"FA00014200000;")

        peer = threading.Thread(target=receiver, daemon=True)
        peer.start()
        tb = gr.top_block()
        cat = astra918.CatSerial(os.ttyname(slave), poll_interval=0.1)
        raw = blocks.message_debug()
        text = blocks.message_debug()
        status = blocks.message_debug()
        tb.msg_connect((cat, "bytes"), (raw, "store"))
        tb.msg_connect((cat, "text"), (text, "store"))
        tb.msg_connect((cat, "status"), (status, "store"))
        try:
            tb.start()
            deadline = time.monotonic() + 2
            while raw.num_messages() == 0 and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertGreater(raw.num_messages(), 0)
            self.assertIn(b"FA;", b"".join(commands))
            self.assertEqual(
                bytes(pmt.u8vector_elements(pmt.cdr(raw.get_message(0)))),
                b"FA00014200000;",
            )
            self.assertEqual(pmt.symbol_to_string(text.get_message(0)), "FA00014200000;")
            self.assertTrue(
                any(
                    pmt.to_python(status.get_message(i)).get("dial_hz") == 14_200_000
                    for i in range(status.num_messages())
                )
            )
        finally:
            tb.stop()
            tb.wait()
            done.set()
            peer.join(timeout=1)
            os.close(master)
            os.close(slave)


class ControlPanelTests(unittest.TestCase):
    def test_external_state_and_live_slider_command(self):
        os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
        from PyQt5 import QtWidgets

        class Receiver:
            def __init__(self):
                self.center = 14_200_000
                self.commands = []

            def state(self):
                return pmt.to_pmt(dict(
                    configured=1, streaming=1, error=0, revision=2,
                    center_hz=self.center, dial_hz=self.center + 1500,
                    audio_offset=1500, mode=2, input=2, audio_low=100,
                    audio_high=3500, rf_auto=1, if_auto=1, rf_gain=20,
                    if_gain=15, lf_gain=4, attenuator=2, capacitor=500,
                    reference=0, gpio=5,
                ))

            def command(self, message):
                self.commands.append(pmt.to_python(message))

        app = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        receiver = Receiver()
        panel = astra918.ControlPanel(receiver)
        try:
            self.assertEqual(panel.center.value(), 14.2)
            self.assertEqual(panel.offset.value(), 1500)
            self.assertEqual(panel.capacitor.value(), 500)
            self.assertTrue(panel.gpio[0].isChecked())
            receiver.center = 14_300_000
            panel._refresh()
            self.assertEqual(panel.center.value(), 14.3)
            panel.capacitor.setValue(501)
            deadline = time.monotonic() + 1
            while not receiver.commands and time.monotonic() < deadline:
                app.processEvents()
                time.sleep(0.01)
            self.assertTrue(receiver.commands)
            self.assertEqual(receiver.commands[0]["name"], "capacitor")
            self.assertEqual(receiver.commands[0]["value"], 501)
        finally:
            panel.close()


if __name__ == "__main__":
    unittest.main()
