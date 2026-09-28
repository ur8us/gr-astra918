#pragma once
#include "cmx918_protocol.h"
#include <algorithm>
#include <complex>
namespace cmx {
struct IqBlock {
  uint32_t generation = 0, revision = 0;
  uint64_t center = 0, dial = 0;
  std::vector<std::complex<float>> samples;
};
class AstraDecoder {
  Bytes pending;
  bool started = false;
  uint32_t generation = 0, sequence = 0;
  uint64_t index = 0;

public:
  std::vector<IqBlock> feed(const Bytes &input) {
    if (input.size() > 65536)
      throw std::runtime_error("Oversized IQ transfer");
    pending.insert(pending.end(), input.begin(), input.end());
    std::vector<IqBlock> result;
    size_t used = 0;
    while (pending.size() - used >= 64) {
      Bytes h(pending.begin() + used, pending.begin() + used + 64);
      // Retunes may leave a partial old frame in USB. Rejoin at the next
      // complete header; sequence continuity remains enforced within an epoch.
      if (!std::equal(h.begin(), h.begin() + 4, "ASIQ") || h[4] != 1 ||
          h[5] != 0x80 || u16(h, 6) != 64 || u32(h, 24) != 120000 ||
          u16(h, 28) != 512) {
        used++;
        continue;
      }
      if (pending.size() - used < 2112)
        break;
      auto gen = u32(h, 8);
      auto seq = u32(h, 12);
      auto first = u64(h, 16);
      if (!started || gen != generation) {
        started = true;
        generation = gen;
        sequence = seq;
        index = first;
      }
      if (seq != sequence || first != index)
        throw std::runtime_error("IQ sample loss");
      IqBlock b;
      b.generation = gen;
      b.revision = u32(h, 48);
      b.center = u64(h, 32);
      b.dial = u64(h, 40);
      b.samples.reserve(512);
      for (size_t i = 0; i < 512; i++) {
        auto p = used + 64 + i * 4;
        b.samples.emplace_back(float(i16(pending, p)) / 32768.f,
                               float(i16(pending, p + 2)) / 32768.f);
      }
      result.push_back(std::move(b));
      sequence++;
      index += 512;
      used += 2112;
    }
    pending.erase(pending.begin(), pending.begin() + used);
    return result;
  }
};
} // namespace cmx
