# gr-astra918

GNU Radio 3.10 out-of-tree blocks for the Astra918 RP2350 receiver. The source
delivers 120 ksps complex I/Q over vendor USB interface 4. Firmware USB audio
and CAT remain available to WSJT-X while GNU Radio runs. One program at a time
may own the vendor I/Q interface.

Build with a C++17 compiler, GNU Radio 3.10, pybind11, Python development
headers, and libusb 1.0. On Linux:

```sh
cmake -S . -B build -DPython3_EXECUTABLE=/usr/bin/python3
cmake --build build -j
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Without system installation, run `./open_workbench.sh` after building. It
starts GNU Radio Companion with this checkout's block definitions and Python
module paths. Running a generated flowgraph from that GRC session inherits the
same paths. System installation makes the blocks available in ordinary GRC
sessions; reopen GRC after installing.

Use a matching GNU Radio, Python, pybind11, and libusb toolchain on Windows
(MSYS2 UCRT64) or macOS. A USB driver must expose the receiver's vendor
interface to libusb; the operating system retains its audio and serial
interfaces. Native builds on those systems require separate verification.

Open `examples/source_to_null.grc` for the receiver workbench. Despite its
historical filename, it now displays the 120 kHz I/Q spectrum and waterfall,
the firmware audio waveform, receiver controls, and status/CAT messages. Set
the audio input in your operating system to Astra918, or set the **OS recording
device** field of the audio block to your audio backend's device name. An empty
field uses the system's default input, which may be a different microphone.
The receiver's firmware audio is mono at 12 ksample/s; some audio backends may
resample to this rate or require an explicit device configuration.

The blocks serve distinct interfaces:

- **Astra918 I/Q Source** outputs complex 120 ksps samples over USB vendor
  interface 4, with `rx_freq` and `generation` stream tags. Its `status` message
  output is a PMT dictionary containing applied receiver settings, dial and
  center frequencies, and errors. It reports changes made by CAT and other
  programs. Its `cmd` input accepts receiver command dictionaries.
- **Astra918 Firmware Audio** outputs mono float samples from the operating
  system's 12 kHz USB sound-card input. It does not use the vendor interface.
- **Astra918 CAT Serial** optionally opens the USB CDC port. It emits complete
  CAT replies on `text` (PMT symbols) and `bytes` (raw byte-vector PDUs), plus
  `status` dictionaries. Send a printable command such as `FA;` to `cmd`. Leave
  its port field empty while WSJT-X owns CAT; the I/Q source still reports
  decoded dial and receiver state. A physical serial port is normally exclusive.
  This block requires Python `pyserial` when a port is selected.
- **Astra918 Receiver Controls** embeds a Qt panel for spectrum center,
  firmware USB audio offset and mode/passband, antenna input, automatic and
  manual gains, LF attenuation and capacitor, oscillator reference, and logical
  GPIO0–GPIO7. It polls the firmware-owned state and follows external tuning.
  Its optional frequency/waterfall sink IDs keep their frequency axes centered
  after external CAT or GUI changes. The panel uses the I/Q source's existing
  USB connection; it does not claim CAT.
- **Astra918 Control** remains available for message-only control flowgraphs.
  It takes the source ID as its `receiver` parameter and forwards `cmd` messages
  through the same USB owner, with a `status` message output.

Commands are PMT dictionaries with a symbol `name` and integer `value`. Names
are `center_hz`, `audio_offset`, `mode` (1 LSB, 2 USB), `reference` (0 Internal,
1 External), `gpio0` through `gpio7`, `input` (0 Auto, 1 LF, 2 HF, 3 VHF),
`rf_auto`, `if_auto`, `rf_gain` (0–38), `if_gain` (0–31), `lf_gain` (0–15),
`attenuator` (0–15), and `capacitor` (0–4095). `audio_filter` takes low as
`value` and an additional integer `high`. `retry` and `save` take only `name`.
Changes apply immediately; only `save` writes receiver flash. `center_hz`
preserves the firmware audio offset, while `audio_offset` preserves the RF
center. Check the returned status for PLL/configuration faults.

The Qt workbench requires GNU Radio Qt GUI and PyQt5. The other examples show
a live spectrum and USB/LSB demodulation from I/Q. The latter requires an audio
output device. Real receiver, OS audio-device selection, and native
Windows/macOS operation still require testing on those platforms.

The protocol, USB transport and I/Q decoder under `lib/vendor/` are MIT code
copied from `astra918sdr-sdrpp` commit `bb945ba6fe85160e8c456fcdb14912433baac268`.
The MIT copyright notice is retained in `LICENSE`. Firmware protocol and
receiver behavior come from the neighboring `astra918sdr` project.
