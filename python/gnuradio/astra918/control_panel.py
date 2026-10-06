"""Qt controls for the receiver-owned settings exposed by Astra918 Source."""

import threading

from PyQt5 import QtCore, QtWidgets
import pmt


class ControlPanel(QtWidgets.QWidget):
    """Poll applied state and send changes without blocking the Qt event loop."""

    def __init__(self, receiver, top_block=None, spectrum_sink="", waterfall_sink=""):
        super().__init__()
        self.receiver = receiver
        self.top_block = top_block
        self.spectrum_sink = spectrum_sink
        self.waterfall_sink = waterfall_sink
        self._updating = False
        self._pending = {}
        self._inflight = None
        self._lock = threading.Lock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._error = ""
        self._last_center = None
        self._worker = threading.Thread(target=self._run_commands, daemon=True)
        self._worker.start()

        layout = QtWidgets.QGridLayout(self)
        row = 0
        self.center = QtWidgets.QDoubleSpinBox()
        self.center.setRange(0.07, 260.0)
        self.center.setDecimals(6)
        self.center.setSingleStep(0.001)
        self.center.setSuffix(" MHz")
        self.center.editingFinished.connect(
            lambda: self._send("center_hz", round(self.center.value() * 1_000_000))
        )
        layout.addWidget(QtWidgets.QLabel("Spectrum center"), row, 0)
        layout.addWidget(self.center, row, 1)
        self.offset = QtWidgets.QSpinBox()
        self.offset.setRange(-59_500, 59_500)
        self.offset.setSuffix(" Hz")
        self.offset.editingFinished.connect(
            lambda: self._send("audio_offset", self.offset.value())
        )
        layout.addWidget(QtWidgets.QLabel("Firmware USB audio offset"), row, 2)
        layout.addWidget(self.offset, row, 3)
        row += 1

        self.mode = self._choice(layout, row, 0, "USB audio mode", ["LSB", "USB"],
                                 lambda index: self._send("mode", index + 1))
        self.input = self._choice(layout, row, 2, "Antenna input",
                                  ["Auto", "LF", "HF", "VHF"],
                                  lambda index: self._send("input", index))
        row += 1
        self.low = self._integer(layout, row, 0, "Audio low edge", 0, 4999)
        self.high = self._integer(layout, row, 2, "Audio high edge", 1, 5000)
        self.low.setValue(100)
        self.high.setValue(3500)
        self.low.editingFinished.connect(self._send_filter)
        self.high.editingFinished.connect(self._send_filter)
        row += 1

        self.rf_auto = self._check(layout, row, 0, "RF auto gain",
                                   lambda checked: self._send("rf_auto", int(checked)))
        self.if_auto = self._check(layout, row, 2, "IF auto gain",
                                   lambda checked: self._send("if_auto", int(checked)))
        row += 1
        self.rf_gain = self._slider(layout, row, 0, "RF gain code", 38, "rf_gain")
        self.if_gain = self._slider(layout, row, 2, "IF gain code", 31, "if_gain")
        row += 1
        self.lf_gain = self._slider(layout, row, 0, "LF gain code", 15, "lf_gain")
        self.attenuator = self._slider(layout, row, 2, "LF attenuator", 15, "attenuator")
        row += 1
        self.capacitor = self._slider(layout, row, 0, "LF/MF capacitor", 4095, "capacitor")
        self.reference = self._choice(layout, row, 2, "38.4 MHz reference",
                                      ["Internal", "External"],
                                      lambda index: self._send("reference", index))
        row += 1

        self.vfo_sign = self._choice(layout, row, 0, "VFO sign",
                                     ["Auto", "LO above the signal", "LO below the signal"],
                                     lambda index: self._send("vfo_sign", index))
        self.if_frequency = self._choice(layout, row, 2, "IF frequency",
                                         ["Auto (96 kHz)", "96 kHz", "120 kHz"],
                                         lambda index: self._send("if_frequency", index))
        row += 1

        gpio_box = QtWidgets.QHBoxLayout()
        gpio_box.addWidget(QtWidgets.QLabel("Logical GPIO"))
        self.gpio = []
        for bit in range(8):
            checkbox = QtWidgets.QCheckBox(str(bit))
            checkbox.toggled.connect(
                lambda checked, bit=bit: self._send(f"gpio{bit}", int(checked))
            )
            gpio_box.addWidget(checkbox)
            self.gpio.append(checkbox)
        gpio_box.addStretch()
        layout.addLayout(gpio_box, row, 0, 1, 4)
        row += 1

        buttons = QtWidgets.QHBoxLayout()
        save = QtWidgets.QPushButton("Save settings to receiver flash")
        save.clicked.connect(lambda: self._send("save"))
        retry = QtWidgets.QPushButton("Retry receiver")
        retry.clicked.connect(lambda: self._send("retry"))
        buttons.addWidget(save)
        buttons.addWidget(retry)
        buttons.addStretch()
        layout.addLayout(buttons, row, 0, 1, 4)
        row += 1
        self.status = QtWidgets.QLabel("Waiting for receiver state")
        self.status.setWordWrap(True)
        layout.addWidget(self.status, row, 0, 1, 4)
        self._banner = None
        if top_block is not None:
            top_block.top_layout.addWidget(self)
            self._banner = QtWidgets.QLabel()
            self._banner.setWordWrap(True)
            self._banner.setStyleSheet("background: #ffe3df; color: #641b12; padding: 8px;")
            top_block.top_layout.insertWidget(0, self._banner)
            self._banner.hide()
            if top_block.width() < 1100 or top_block.height() < 750:
                top_block.resize(max(top_block.width(), 1180),
                                 max(top_block.height(), 850))

        self._timer = QtCore.QTimer(self)
        self._timer.timeout.connect(self._refresh)
        self._timer.start(200)
        self._refresh()

    @staticmethod
    def _choice(layout, row, col, label, choices, callback):
        widget = QtWidgets.QComboBox()
        widget.addItems(choices)
        widget.currentIndexChanged.connect(callback)
        layout.addWidget(QtWidgets.QLabel(label), row, col)
        layout.addWidget(widget, row, col + 1)
        return widget

    @staticmethod
    def _integer(layout, row, col, label, minimum, maximum):
        widget = QtWidgets.QSpinBox()
        widget.setRange(minimum, maximum)
        widget.setSuffix(" Hz")
        layout.addWidget(QtWidgets.QLabel(label), row, col)
        layout.addWidget(widget, row, col + 1)
        return widget

    @staticmethod
    def _check(layout, row, col, label, callback):
        widget = QtWidgets.QCheckBox(label)
        widget.toggled.connect(callback)
        layout.addWidget(widget, row, col, 1, 2)
        return widget

    def _slider(self, layout, row, col, label, maximum, command):
        holder = QtWidgets.QWidget()
        line = QtWidgets.QHBoxLayout(holder)
        line.setContentsMargins(0, 0, 0, 0)
        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(0, maximum)
        value = QtWidgets.QLabel("0")
        value.setMinimumWidth(35)
        slider._value_label = value
        slider.valueChanged.connect(lambda n: value.setText(str(n)))
        slider.valueChanged.connect(lambda n: self._send(command, n))
        line.addWidget(slider)
        line.addWidget(value)
        layout.addWidget(QtWidgets.QLabel(label), row, col)
        layout.addWidget(holder, row, col + 1)
        return slider

    def _send_filter(self):
        self._send("audio_filter", self.low.value(), self.high.value())

    def _send(self, name, value=None, high=None):
        if self._updating:
            return
        with self._lock:
            self._pending[name] = (value, high)
        self._wake.set()

    def _run_commands(self):
        while not self._stop.is_set():
            self._wake.wait(0.2)
            if self._stop.is_set():
                break
            with self._lock:
                if not self._pending:
                    self._wake.clear()
                    continue
                name = next(iter(self._pending))
                value, high = self._pending.pop(name)
                self._inflight = name
            message = pmt.dict_add(pmt.make_dict(), pmt.intern("name"), pmt.intern(name))
            if value is not None:
                message = pmt.dict_add(message, pmt.intern("value"), pmt.from_long(value))
            if high is not None:
                message = pmt.dict_add(message, pmt.intern("high"), pmt.from_long(high))
            try:
                self.receiver.command(message)
                error = ""
            except Exception as exc:
                error = str(exc)
            with self._lock:
                self._inflight = None
                self._error = error

    def _adopt(self, widget, value, name=None):
        with self._lock:
            busy = name in self._pending or name == self._inflight
        editing_text = isinstance(widget, (QtWidgets.QSpinBox, QtWidgets.QDoubleSpinBox)) and (
            widget.hasFocus() and widget.lineEdit().isModified()
        )
        if busy or editing_text or (
            isinstance(widget, QtWidgets.QSlider) and widget.isSliderDown()
        ):
            return
        widget.blockSignals(True)
        if isinstance(widget, QtWidgets.QCheckBox):
            widget.setChecked(value)
        elif isinstance(widget, QtWidgets.QComboBox):
            widget.setCurrentIndex(value)
        else:
            widget.setValue(value)
        if isinstance(widget, QtWidgets.QSlider):
            widget._value_label.setText(str(value))
        widget.blockSignals(False)

    def _refresh(self):
        try:
            state = pmt.to_python(self.receiver.state())
        except Exception as exc:
            self.status.setText(f"Receiver state unavailable: {exc}")
            return
        self._updating = True
        try:
            center = int(state["center_hz"])
            self._adopt(self.center, center / 1_000_000, "center_hz")
            self._adopt(self.offset, int(state["audio_offset"]), "audio_offset")
            self._adopt(self.mode, int(state["mode"]) - 1, "mode")
            self._adopt(self.input, int(state["input"]), "input")
            self._adopt(self.low, int(state["audio_low"]), "audio_filter")
            self._adopt(self.high, int(state["audio_high"]), "audio_filter")
            self._adopt(self.rf_auto, bool(state["rf_auto"]), "rf_auto")
            self._adopt(self.if_auto, bool(state["if_auto"]), "if_auto")
            for name in ("rf_gain", "if_gain", "lf_gain", "attenuator", "capacitor"):
                self._adopt(getattr(self, name), int(state[name]), name)
            self._adopt(self.reference, int(state["reference"]), "reference")
            self._adopt(self.vfo_sign, int(state["vfo_sign"]), "vfo_sign")
            self._adopt(self.if_frequency, int(state["if_frequency"]), "if_frequency")
            self.vfo_sign.setEnabled(bool(state.get("vfo_if_selection", False)))
            self.if_frequency.setEnabled(bool(state.get("vfo_if_selection", False)))
            mask = int(state["gpio"])
            for bit, checkbox in enumerate(self.gpio):
                self._adopt(checkbox, bool(mask & (1 << bit)), f"gpio{bit}")
            self.rf_gain.setEnabled(not state["rf_auto"])
            self.if_gain.setEnabled(not state["if_auto"])
            if center != self._last_center:
                sinks_ready = True
                for sink_name in (self.spectrum_sink, self.waterfall_sink):
                    sink = getattr(self.top_block, sink_name, None)
                    if sink is not None:
                        sink.set_frequency_range(center, 120_000)
                    elif sink_name:
                        sinks_ready = False
                if sinks_ready:
                    self._last_center = center
        finally:
            self._updating = False
        with self._lock:
            error = self._error
        transport_error = state.get("transport_error", "")
        if self._banner is not None:
            self._banner.setText(f"Astra918 I/Q unavailable: {transport_error}")
            self._banner.setVisible(bool(transport_error))
        self.status.setText(
            f"Dial {state['dial_hz']} Hz · center {state['center_hz']} Hz · "
            f"revision {state['revision']} · "
            f"configured {'yes' if state['configured'] else 'no'} · "
            f"I/Q {'on' if state['streaming'] else 'off'} · "
            f"error {state['error']}"
            + (f" · USB: {transport_error}" if transport_error else "")
            + (f" · command: {error}" if error else "")
        )

    def closeEvent(self, event):
        self._stop.set()
        self._wake.set()
        super().closeEvent(event)
