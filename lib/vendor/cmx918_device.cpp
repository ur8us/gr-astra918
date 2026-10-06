#include "cmx918_device.h"
#include <chrono>
namespace cmx {
Device::Device(std::unique_ptr<Transport> t) : transport(std::move(t)) {
  capabilities = Capabilities::decode(command(Caps));
  refresh();
}
Bytes Device::command(uint8_t cmd, const Bytes &p) {
  std::lock_guard<std::mutex> guard(control_mutex);
  if (!usable)
    throw std::runtime_error(
        "Control connection lost synchronization; reconnect");
  try {
    auto seq = ++sequence;
    transport->write_control(record(cmd, seq, p));
    Bytes b;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (b.size() < record_size) {
      if (std::chrono::steady_clock::now() >= deadline)
        throw std::runtime_error(
            "Control timeout; reconnect receiver before retry");
      auto part = transport->read_control(record_size - b.size(), 100);
      if (part.size() > record_size - b.size())
        throw std::runtime_error("Oversized control read");
      b.insert(b.end(), part.begin(), part.end());
    }
    return response(b, cmd, seq);
  } catch (const ProtocolError &) {
    throw;
  } catch (...) {
    usable = false;
    throw;
  }
}
State Device::refresh() {
  state = State::decode(command(Status));
  capabilities.reference_clock = state.features & 64;
  capabilities.logical_gpio = state.features & 128;
  capabilities.vfo_if_selection = state.features & 32;
  return state;
}
void Device::frequency(uint64_t v) {
  state = State::decode(command(SetFrequency, integer(v, 8)));
}
void Device::input(uint8_t v) { state = State::decode(command(SetInput, {v})); }
void Device::gain_mode(uint8_t b, bool a) {
  state = State::decode(command(SetGainMode, {b, uint8_t(!a)}));
}
void Device::gain(uint8_t b, uint8_t v) {
  state = State::decode(command(SetGain, {b, v}));
}
void Device::lf_gain(uint8_t code) {
  if (!capabilities.manual_lf_rf)
    throw std::runtime_error("Receiver does not support manual LF gain");
  if (code > 15)
    throw std::runtime_error("LF RF gain code must be 0..15");
  state = State::decode(command(SetGain, {2, code}));
}
void Device::lf_attenuator(uint8_t code) {
  if (!capabilities.manual_lf_rf)
    throw std::runtime_error("Receiver does not support manual LF attenuation");
  if (code > 15)
    throw std::runtime_error("LF attenuator code must be 0..15");
  state = State::decode(command(SetGain, {3, code}));
}
void Device::lf_mf_capacitor(uint16_t code) {
  if (!capabilities.lf_mf_capacitor)
    throw std::runtime_error("Receiver does not support capacitor tuning");
  if (code > 4095)
    throw std::runtime_error("LF/MF capacitor code must be 0..4095");
  state = State::decode(command(SetLfMfCapacitor, integer(code, 2)));
  if (!state.configured || state.lf_mf_capacitor != code)
    throw std::runtime_error("Receiver did not apply LF/MF capacitor tuning");
}
void Device::start() {
  // A previous owner or a stalled transfer can leave an incomplete frame in
  // the USB FIFO even after reconnect. Establish a clean boundary before the
  // new generation; never reset the composite device or its audio/CAT paths.
  stop();
  state = State::decode(command(Start));
}
void Device::stop() {
  state = State::decode(command(Stop));
  // Caller joins its I/Q reader first. STOP may leave a partial old frame in
  // the host and device queues. Drain to a quiet interval before parser reset.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!transport->read_iq(16384, 30).empty()) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error("I/Q did not quiesce after STOP; reconnect");
  }
}
Bytes Device::read_iq() { return transport->read_iq(16384, 100); }
} // namespace cmx
