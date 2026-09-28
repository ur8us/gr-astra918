#!/usr/bin/env python3
"""Live 120 ksps Astra918 spectrum; dial and waterfall follow external CAT."""
import argparse
import sys
from PyQt5 import Qt, sip
from gnuradio import gr, blocks, qtgui, astra918
from gnuradio.fft import window
import pmt


def command(block, name, value):
    msg = pmt.dict_add(pmt.make_dict(), pmt.intern("name"), pmt.intern(name))
    msg = pmt.dict_add(msg, pmt.intern("value"), pmt.from_long(value))
    block.to_basic_block()._post(pmt.intern("cmd"), msg)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", default="")
    args = parser.parse_args()
    app = Qt.QApplication(sys.argv)
    tb = gr.top_block("Astra918 spectrum")
    src = astra918.source.make(args.serial)
    ctl = astra918.control.make(src)
    status = blocks.message_debug()
    tb.msg_connect((ctl, "status"), (status, "store"))
    freq = qtgui.freq_sink_c(2048, window.WIN_BLACKMAN_hARRIS, 14.2e6, 120e3, "Spectrum", 1)
    waterfall = qtgui.waterfall_sink_c(1024, window.WIN_BLACKMAN_hARRIS, 14.2e6, 120e3, "Waterfall", 1)
    tb.connect(src, freq)
    tb.connect(src, waterfall)
    widget = Qt.QWidget()
    widget.setWindowTitle("Astra918 spectrum")
    layout = Qt.QVBoxLayout(widget)
    tune = Qt.QDoubleSpinBox()
    tune.setRange(0.07, 170.0)
    tune.setDecimals(6)
    tune.setSuffix(" MHz")
    tune.setValue(14.2)
    tune.valueChanged.connect(lambda mhz: command(ctl, "center_hz", round(mhz * 1e6)))
    layout.addWidget(tune)
    layout.addWidget(sip.wrapinstance(freq.qwidget(), Qt.QWidget))
    layout.addWidget(sip.wrapinstance(waterfall.qwidget(), Qt.QWidget))
    widget.resize(900, 760)
    tb.start()

    def adopt_receiver():
        center = src.center_freq()
        if abs(tune.value() * 1e6 - center) >= 1:
            tune.blockSignals(True)
            tune.setValue(center / 1e6)
            tune.blockSignals(False)
            freq.set_frequency_range(center, 120e3)
            waterfall.set_frequency_range(center, 120e3)

    timer = Qt.QTimer()
    timer.timeout.connect(adopt_receiver)
    timer.start(200)
    widget.show()
    try:
        return app.exec_()
    finally:
        tb.stop()
        tb.wait()


if __name__ == "__main__":
    raise SystemExit(main())
