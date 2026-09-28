#pragma once
#include "usb_transport.h"
#include <mutex>
namespace cmx {
class Device {
  std::unique_ptr<Transport> transport;
  std::mutex control_mutex;
  uint32_t sequence = 0;
  bool usable = true;

public:
  Capabilities capabilities;
  State state;
  explicit Device(std::unique_ptr<Transport>);
  Bytes command(uint8_t, const Bytes & = {});
  State refresh();
  void frequency(uint64_t);
  void input(uint8_t);
  void gain_mode(uint8_t block, bool automatic);
  void gain(uint8_t block, uint8_t code);
  void lf_gain(uint8_t code);
  void lf_attenuator(uint8_t code);
  void lf_mf_capacitor(uint16_t code);
  void start();
  void stop();
  Bytes read_iq();
};
} // namespace cmx
