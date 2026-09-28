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

Use a matching GNU Radio, Python, pybind11, and libusb toolchain on Windows
(MSYS2 UCRT64) or macOS. A USB driver must expose the receiver's vendor
interface to libusb; the operating system retains its audio and serial
interfaces. Native builds on those systems require separate verification.

`Astra918 Source` has one complex stream output and a `status` message output.
It produces `rx_freq` and `generation` tags at I/Q frame boundaries. The
`Astra918 Control` block takes the source block as its `receiver` parameter and
forwards `cmd` messages through the same USB owner. Both blocks expose current
settings through PMT dictionary status messages; receiver state is adopted on
connection and after CAT changes. Connect the control block's `status` output
to a message sink to include it in the running flowgraph.

Commands are PMT dictionaries with a symbol `name` and integer `value`. Names
are `center_hz`, `audio_offset`, `mode` (1 LSB, 2 USB), `reference` (0 Internal,
1 External), `gpio0` through `gpio7`, `input` (0 Auto, 1 LF, 2 HF, 3 VHF),
`rf_auto`, `if_auto`, `rf_gain` (0–38), `if_gain` (0–31), `lf_gain` (0–15),
`attenuator` (0–15), and `capacitor` (0–4095). `audio_filter` takes low as
`value` and an additional integer `high`. `retry` and `save` take only `name`.
Changes apply immediately; only `save` writes receiver flash. `center_hz`
preserves the firmware audio offset, while `audio_offset` preserves the RF
center. Check the returned status for PLL/configuration faults.

The examples show a live spectrum and a USB/LSB audio channel. They require
GNU Radio Qt GUI, PyQt5 and, for the audio example, an audio output device.

The protocol, USB transport and I/Q decoder under `lib/vendor/` are MIT code
copied from `astra918sdr-sdrpp` commit `bb945ba6fe85160e8c456fcdb14912433baac268`.
The MIT copyright notice is retained in `LICENSE`. Firmware protocol and
receiver behavior come from the neighboring `astra918sdr` project.
