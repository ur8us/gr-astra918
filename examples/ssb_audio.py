#!/usr/bin/env python3
"""Demodulate a narrow USB or LSB channel from the 120 ksps I/Q stream."""
import argparse
import time
from gnuradio import gr, astra918, audio, blocks, filter
import pmt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", default="")
    parser.add_argument("--center", type=int, default=14_200_000)
    parser.add_argument("--channel-offset", type=int, default=1500)
    parser.add_argument("--sideband", choices=("USB", "LSB"), default="USB")
    args = parser.parse_args()
    if not 70_000 <= args.center <= 260_000_000:
        parser.error("center is outside the receiver tuning range")
    if not -50_000 <= args.channel_offset <= 50_000:
        parser.error("channel offset must fit inside the I/Q passband")
    tb = gr.top_block("Astra918 SSB audio")
    src = astra918.source.make(args.serial)
    ctl = astra918.control.make(src)
    status = blocks.message_debug()
    tb.msg_connect((ctl, "status"), (status, "store"))
    side = 1 if args.sideband == "USB" else -1
    audio_center = args.channel_offset + side * 1600
    taps = filter.firdes.low_pass(1, 120_000, 3300, 800)
    select = filter.freq_xlating_fir_filter_ccf(5, taps, audio_center, 120_000)
    real = blocks.complex_to_real(1)
    speaker = audio.sink(24_000, "", True)
    tb.connect(src, select, real, speaker)
    tb.start()
    msg = pmt.dict_add(pmt.make_dict(), pmt.intern("name"), pmt.intern("center_hz"))
    msg = pmt.dict_add(msg, pmt.intern("value"), pmt.from_long(args.center))
    ctl.to_basic_block()._post(pmt.intern("cmd"), msg)
    try:
        while True:
            time.sleep(1)
            print(f"Spectrum center: {src.center_freq() / 1e6:.6f} MHz", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        tb.stop()
        tb.wait()


if __name__ == "__main__":
    main()
