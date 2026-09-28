#include "cmx918_protocol.h"
#include <algorithm>
namespace cmx {
static void bound(const Bytes &b, size_t o, size_t n) {
  if (o > b.size() || n > b.size() - o)
    throw std::runtime_error("Truncated protocol field");
}
uint16_t u16(const Bytes &b, size_t o) {
  bound(b, o, 2);
  return uint16_t(b[o]) | (uint16_t(b[o + 1]) << 8);
}
uint32_t u32(const Bytes &b, size_t o) {
  bound(b, o, 4);
  return uint32_t(u16(b, o)) | (uint32_t(u16(b, o + 2)) << 16);
}
uint64_t u64(const Bytes &b, size_t o) {
  bound(b, o, 8);
  return uint64_t(u32(b, o)) | (uint64_t(u32(b, o + 4)) << 32);
}
int16_t i16(const Bytes &b, size_t o) {
  auto v = u16(b, o);
  return int16_t(v < 32768 ? int(v) : int(v) - 65536);
}
void put(Bytes &b, size_t o, uint64_t v, size_t n) {
  if (n > 8)
    throw std::runtime_error("Integer field exceeds 64 bits");
  bound(b, o, n);
  for (size_t i = 0; i < n; i++)
    b[o + i] = uint8_t(v >> (8 * i));
}
Bytes integer(uint64_t v, size_t n) {
  Bytes b(n);
  put(b, 0, v, n);
  return b;
}
ProtocolError::ProtocolError(ErrorCode c)
    : std::runtime_error(
          "Astra918 error " + std::to_string(unsigned(c)) + ": " +
          std::array<const char *, 11>{
              "OK", "invalid command", "bad version", "bad length",
              "invalid argument",
              "unsupported command", "receiver busy", "channel exceeds spectrum bounds",
              "CMX918 I2C error", "PLL/calibration timeout", "internal error"}
              .at(unsigned(c))),
      code(c) {}
Bytes record(uint8_t cmd, uint32_t seq, const Bytes &p, ErrorCode e) {
  if (p.size() > 240)
    throw std::runtime_error("Control payload too large");
  Bytes b(256);
  b[0] = 'A';
  b[1] = 'S';
  b[2] = 'T';
  b[3] = '1';
  b[4] = 1;
  b[5] = cmd;
  b[6] = uint8_t(e);
  put(b, 8, seq, 4);
  put(b, 12, p.size(), 2);
  std::copy(p.begin(), p.end(), b.begin() + 16);
  return b;
}
Bytes response(const Bytes &b, uint8_t cmd, uint32_t seq) {
  if (b.size() != 256 || !std::equal(b.begin(), b.begin() + 4, "AST1") ||
      b[4] != 1 || b[5] != cmd || u32(b, 8) != seq)
    throw std::runtime_error("Mismatched USB reply");
  auto n = u16(b, 12);
  if (n > 240 || b[7] || u16(b, 14) ||
      std::any_of(b.begin() + 16 + n, b.end(), [](auto v) { return v != 0; }))
    throw std::runtime_error("Malformed USB reply");
  if (b[6] > 10)
    throw std::runtime_error("Unknown device error");
  if (b[6])
    throw ProtocolError(ErrorCode(b[6]));
  return Bytes(b.begin() + 16, b.begin() + 16 + n);
}
Capabilities Capabilities::decode(const Bytes &b) {
  if (b.size() != 204 || u32(b, 0) != 2 || b[29] != 39 || b[30] != 32 ||
      !(b[31] & 1) || b[199] != 1 || u32(b, 194) != 40 ||
      u32(b, 200) != 512)
    throw std::runtime_error("Unsupported capability layout");
  Capabilities c;
  c.minimum = u32(b, 4);
  c.maximum = u32(b, 8);
  c.step = u32(b, 12);
  c.candidate = u32(b, 16);
  c.qualified = u32(b, 20);
  c.failed = u32(b, 24);
  c.inputs = b[28];
  c.filter_mode = b[31] & 1;
  c.usb_decimated_120 = b[31] & 2;
  c.wide_fir = b[31] & 4;
  c.lf_mf_capacitor = b[31] & 8;
  c.register_access = b[31] & 16;
  c.manual_lf_rf = b[31] & 32;
  c.manual_rf_inputs = b[198];
  if (c.minimum > c.maximum || !c.step ||
      (c.candidate | c.qualified | c.failed) & ~31u)
    throw std::runtime_error("Invalid capabilities");
  for (size_t i = 0; i < 5; i++) {
    auto r = u32(b, 32 + i * 4);
    if (r == 0 || r > 240000)
      throw std::runtime_error("Unsafe advertised rate");
    c.rates.push_back(r);
  }
  for (size_t i = 0; i < 39; i++)
    c.rf_gains.push_back(i16(b, 52 + i * 2));
  for (size_t i = 0; i < 32; i++)
    c.if_gains.push_back(i16(b, 130 + i * 2));
  return c;
}
std::vector<uint32_t> Capabilities::selectable_rates(bool experimental,
                                                     bool failed_rates) const {
  auto mask = qualified | (experimental ? candidate : 0);
  if (!failed_rates)
    mask &= ~failed;
  std::vector<uint32_t> out;
  for (size_t i = 0; i < rates.size(); i++)
    if (mask & (1u << i))
      out.push_back(rates[i]);
  return out;
}
State State::decode(const Bytes &b) {
  if (b.size() != 128)
    throw std::runtime_error("Invalid state length");
  State s;
  s.center = u64(b, 80);
  const auto bits = u32(b, 88);
  s.offset =
      bits <= INT32_MAX ? int32_t(bits) : int32_t(int64_t(bits) - 4294967296LL);
  s.audio_mode = b[92];
  s.audio_low = u16(b, 94);
  s.audio_high = u16(b, 116);
  s.reference_clock = b[118];
  s.gpio = b[119];
  s.features = b[120];
  s.revision = u32(b, 96);
  s.audio_under = u32(b, 100);
  s.audio_over = u32(b, 104);
  s.audio_stalls = u32(b, 108);
  s.saved_revision = u32(b, 112);
  if (s.reference_clock > 1 || (s.audio_mode != 1 && s.audio_mode != 2) || s.audio_low >= s.audio_high ||
      s.audio_high > 5000)
    throw std::runtime_error("Invalid audio state");
  s.requested = u64(b, 0);
  s.actual = u64(b, 8);
  s.rate = u32(b, 16);
  s.bandwidth = u32(b, 20);
  s.options = u32(b, 24);
  s.input = b[28];
  s.resolved_input = b[29];
  s.rf_mode = b[30];
  s.if_mode = b[31];
  s.rf_gain = b[32];
  s.if_gain = b[33];
  s.streaming = b[34];
  s.configured = b[35];
  s.generation = u32(b, 36);
  s.dropped = u32(b, 40);
  s.capture_faults = u32(b, 44);
  s.usb_faults = u32(b, 48);
  s.error = u32(b, 52);
  s.chip_status = b[56];
  s.chip_valid = b[57];
  s.rssi = i16(b, 58);
  s.sequence = u32(b, 60);
  s.lf_mf_capacitor = u16(b, 76);
  s.lf_gain = b[78];
  s.lf_attenuator = b[79];
  if (s.input > 3 || s.resolved_input < 1 || s.resolved_input > 3 ||
      s.rf_mode > 1 || s.if_mode > 1 || s.rf_gain > 38 || s.if_gain > 31 ||
      b[34] > 1 || b[35] > 1 || s.lf_mf_capacitor > 4095 || s.lf_gain > 15 ||
      s.lf_attenuator > 15)
    throw std::runtime_error("Invalid state fields");
  return s;
}
Bytes State::encode() const {
  Bytes b(128);
  put(b, 80, center, 8);
  put(b, 88, uint32_t(offset), 4);
  b[92] = audio_mode;
  put(b, 94, audio_low, 2);
  put(b, 96, revision, 4);
  put(b, 100, audio_under, 4);
  put(b, 104, audio_over, 4);
  put(b, 108, audio_stalls, 4);
  put(b, 112, saved_revision, 4);
  put(b, 116, audio_high, 2);
  b[118] = reference_clock;
  b[119] = gpio;
  b[120] = features;
  put(b, 0, requested, 8);
  put(b, 8, actual, 8);
  put(b, 16, rate, 4);
  put(b, 20, bandwidth, 4);
  put(b, 24, options, 4);
  b[28] = input;
  b[29] = resolved_input;
  b[30] = rf_mode;
  b[31] = if_mode;
  b[32] = rf_gain;
  b[33] = if_gain;
  b[34] = streaming;
  b[35] = configured;
  put(b, 36, generation, 4);
  put(b, 40, dropped, 4);
  put(b, 44, capture_faults, 4);
  put(b, 48, usb_faults, 4);
  put(b, 52, error, 4);
  b[56] = chip_status;
  b[57] = chip_valid;
  put(b, 58, uint16_t(rssi), 2);
  put(b, 60, sequence, 4);
  put(b, 64, UINT32_MAX, 4);
  put(b, 68, UINT64_MAX, 8);
  put(b, 76, lf_mf_capacitor, 2);
  b[78] = lf_gain;
  b[79] = lf_attenuator;
  return b;
}
std::vector<uint32_t> preset_bandwidths(uint32_t rate) {
  switch (rate) {
  case 12000:
    return {5000};
  case 24000:
    return {5000, 10000};
  case 48000:
    return {10000, 20000};
  case 96000:
    return {20000};
  case 120000:
    return {100000}; // CMX918 240 ksps, explicitly decimated before USB.
  case 240000:
    return {100000};
  default:
    throw ProtocolError(ErrorCode::Bandwidth);
  }
}
uint8_t resolve_input(uint8_t i, uint64_t hz) {
  return i ? i : (hz < 2000000 ? 1 : hz < 40000000 ? 2 : 3);
}
bool lf_mf_capacitor_route(uint8_t selected_input, uint8_t resolved_input) {
  return selected_input == 1 || (selected_input == 0 && resolved_input == 1);
}
double lf_mf_capacitance_pf(uint16_t code) {
  if (code > 4095)
    throw std::runtime_error("LF/MF capacitor code must be 0..4095");
  return 3.0 + 241.0 * code / 4095.0;
}
} // namespace cmx
