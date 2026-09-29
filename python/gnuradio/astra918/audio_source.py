"""Capture the receiver's independent 12 kHz USB sound-card input."""

from gnuradio import audio, gr


class AudioSource(gr.hier_block2):
    """Mono float stream from the OS audio device selected by the user.

    The receiver's UAC1 interface belongs to the operating system, not to the
    vendor I/Q transport. The device name follows GNU Radio Audio Source rules.
    """

    sample_rate = 12_000

    def __init__(self, device_name=""):
        gr.hier_block2.__init__(
            self,
            "Astra918 firmware audio",
            gr.io_signature(0, 0, 0),
            gr.io_signature(1, 1, gr.sizeof_float),
        )
        self._capture = audio.source(self.sample_rate, device_name, True)
        self.connect((self._capture, 0), (self, 0))
