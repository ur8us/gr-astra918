#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace cmx {
using Bytes = std::vector<uint8_t>;
constexpr uint16_t vid = 0xc0de, pid = 0x091a;
constexpr int interface_number = 4;
constexpr uint8_t command_ep = 3, reply_ep = 0x84, iq_ep = 0x85;
constexpr size_t record_size = 256;
enum Command : uint8_t {
  Version = 0x10,
  Info = 0x11,
  Caps = 0x12,
  Status = 0x13,
  SetFrequency = 0x20,
  GetFrequency = 0x21,
  SetRate = 0x22,
  GetRate = 0x23,
  SetInput = 0x24,
  GetInput = 0x25,
  SetGainMode = 0x26,
  SetGain = 0x27,
  GetGain = 0x28,
  SetFilter = 0x29,
  GetFilter = 0x2a,
  UploadFir = 0x2b,
  Options = 0x2c,
  SetLfMfCapacitor = 0x2d,
  GetLfMfCapacitor = 0x2e,
  Start = 0x30,
  Stop = 0x31,
  Bootsel = 0x32,
  ReadRegister = 0x40,
  WriteRegister = 0x41,
  SetOffset = 0x33,
  SetAudioMode = 0x34,
  SetAudioFilter = 0x35,
  Save = 0x36,
  Retry = 0x37,
  TuneChannel = 0x38,
  TuneCenter = 0x39,
  SetReference = 0x3a,
  UpdateGpio = 0x3b
};
enum class ErrorCode : uint8_t {
  Ok,
  Command,
  Version,
  Length,
  Argument,
  Unsupported,
  Busy,
  Bandwidth,
  Io,
  Pll,
  Internal
};
struct ProtocolError : std::runtime_error {
  ErrorCode code;
  explicit ProtocolError(ErrorCode c);
};
uint16_t u16(const Bytes &, size_t);
uint32_t u32(const Bytes &, size_t);
uint64_t u64(const Bytes &, size_t);
int16_t i16(const Bytes &, size_t);
void put(Bytes &, size_t, uint64_t, size_t);
Bytes integer(uint64_t, size_t);
Bytes record(uint8_t, uint32_t, const Bytes & = {}, ErrorCode = ErrorCode::Ok);
Bytes response(const Bytes &, uint8_t, uint32_t);
struct Capabilities {
  uint32_t minimum = 0, maximum = 0, step = 0, candidate = 0, qualified = 0,
           failed = 0;
  uint8_t inputs = 0, filter_mode = 0, manual_rf_inputs = 0;
  bool usb_decimated_120 = false, wide_fir = false, lf_mf_capacitor = false;
  bool manual_lf_rf = false;
  bool register_access = false, reference_clock = false, logical_gpio = false;
  std::vector<uint32_t> rates;
  std::vector<int16_t> rf_gains, if_gains;
  static Capabilities decode(const Bytes &);
  std::vector<uint32_t> selectable_rates(bool experimental,
                                         bool failed_rates = false) const;
};
struct State {
  uint64_t center = 14200000;
  int32_t offset = 0;
  uint8_t audio_mode = 2;
  uint16_t audio_low = 100, audio_high = 3500;
  uint32_t revision = 0, audio_under = 0, audio_over = 0, audio_stalls = 0,
           saved_revision = UINT32_MAX;
  uint64_t requested = 14200000, actual = 14200000;
  uint32_t rate = 120000, bandwidth = 100000, options = 0x18b;
  uint8_t input = 0, resolved_input = 2, rf_mode = 0, if_mode = 0, rf_gain = 38,
          if_gain = 31, lf_gain = 15, lf_attenuator = 15;
  bool streaming = false, configured = false;
  uint32_t generation = 0, dropped = 0, capture_faults = 0, usb_faults = 0,
           error = 0;
  uint8_t chip_status = 0;
  bool chip_valid = false;
  int16_t rssi = 0;
  uint32_t sequence = 0;
  uint16_t lf_mf_capacitor = 0;
  uint8_t reference_clock = 0, gpio = 0, features = 0;
  static State decode(const Bytes &);
  Bytes encode() const;
};
std::vector<uint32_t> preset_bandwidths(uint32_t rate);
uint8_t resolve_input(uint8_t, uint64_t);
// LF/MF capacitor hardware is relevant only for an explicit LF selection or
// when Auto resolves to LF. This is intentionally based on the device's
// applied route, not a host-side frequency estimate.
bool lf_mf_capacitor_route(uint8_t selected_input, uint8_t resolved_input);
// Endpoint interpolation requested by the owner; nominal, not calibrated.
double lf_mf_capacitance_pf(uint16_t code);
} // namespace cmx
