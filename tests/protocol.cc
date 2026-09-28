#include "astra_stream.h"
#include <stdexcept>
#include <algorithm>
static void check(bool value) { if (!value) throw std::runtime_error("Astra918 protocol test failed"); }
int main() {
    cmx::State s;
    s.reference_clock = 1;
    s.gpio = 0xa5;
    s.features = 0xc0;
    s.center = 170000000;
    s.requested = 170000000;
    auto d = cmx::State::decode(s.encode());
    check(d.reference_clock == 1 && d.gpio == 0xa5 && d.features == 0xc0 && d.center == 170000000);
    auto request = cmx::record(cmx::UpdateGpio, 17, {0x04, 0x04});
    check(cmx::response(request, cmx::UpdateGpio, 17) == cmx::Bytes({0x04, 0x04}));
    cmx::Bytes frame(2112);
    std::copy_n("ASIQ", 4, frame.begin());
    frame[4] = 1; frame[5] = 0x80;
    cmx::put(frame, 6, 64, 2);
    cmx::put(frame, 8, 1, 4);
    cmx::put(frame, 24, 120000, 4);
    cmx::put(frame, 28, 512, 2);
    cmx::put(frame, 32, 170000000, 8);
    cmx::put(frame, 40, 170000000, 8);
    cmx::put(frame, 64, 0x8000, 2);
    cmx::put(frame, 66, 32767, 2);
    cmx::AstraDecoder decoder;
    auto half = cmx::Bytes(frame.begin(), frame.begin() + 100);
    check(decoder.feed(half).empty());
    auto rest = cmx::Bytes(frame.begin() + 100, frame.end());
    auto blocks = decoder.feed(rest);
    check(blocks.size() == 1 && blocks[0].samples.size() == 512);
    check(blocks[0].center == 170000000);
    check(blocks[0].samples[0].real() == -1.f);
}
