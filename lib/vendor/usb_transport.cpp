#include "usb_transport.h"
#include <algorithm>
#include <libusb.h>
namespace cmx {
namespace {
void check(int code, const char *operation) {
  if (code < 0)
    throw std::runtime_error(std::string(operation) + ": " +
                             libusb_error_name(code));
}
struct Context {
  libusb_context *p = nullptr;
  Context() { check(libusb_init(&p), "libusb init"); }
  ~Context() { libusb_exit(p); }
};
struct List {
  libusb_device **p = nullptr;
  ssize_t n;
  explicit List(Context &c) : n(libusb_get_device_list(c.p, &p)) {
    check(int(n), "list USB");
  }
  ~List() { libusb_free_device_list(p, 1); }
};
std::string serial_of(libusb_device_handle *h, uint8_t index) {
  unsigned char b[256];
  int n = libusb_get_string_descriptor_ascii(h, index, b, sizeof(b));
  check(n, "read serial");
  return std::string(reinterpret_cast<char *>(b), size_t(n));
}
class Usb final : public Transport {
  Context context;
  libusb_device_handle *handle = nullptr;
  bool claimed = false;
  Bytes read(uint8_t ep, size_t n, unsigned timeout) {
    if (n == 0 || n > 65536)
      throw std::runtime_error("USB read size out of bounds");
    Bytes b(n);
    int actual = 0;
    int rc =
        libusb_bulk_transfer(handle, ep, b.data(), int(n), &actual, timeout);
    // libusb can return valid partial bytes on timeout; never discard them.
    if (rc != LIBUSB_ERROR_TIMEOUT)
      check(rc, "USB read");
    b.resize(size_t(actual));
    return b;
  }

public:
  explicit Usb(const std::string &serial) {
    List list(context);
    try {
      for (ssize_t i = 0; i < list.n; i++) {
        libusb_device_descriptor d{};
        check(libusb_get_device_descriptor(list.p[i], &d), "descriptor");
        if (d.idVendor != vid || d.idProduct != pid)
          continue;
        libusb_device_handle *h = nullptr;
        check(libusb_open(list.p[i], &h), "open CMX918 (check udev/WinUSB)");
        std::string found;
        try {
          found = serial_of(h, d.iSerialNumber);
        } catch (...) {
          libusb_close(h);
          throw;
        }
        if (!serial.empty() && serial != found) {
          libusb_close(h);
          continue;
        }
        if (handle) {
          libusb_close(h);
          throw std::runtime_error(
              "Multiple receivers: select a serial number");
        }
        handle = h;
      }
      if (!handle)
        throw std::runtime_error("CMX918 receiver not found");
      int active = 0;
      check(libusb_get_configuration(handle, &active), "USB configuration");
      if (active != 1)
        throw std::runtime_error("Astra918 USB is not configured; reconnect the device");
      check(libusb_claim_interface(handle, interface_number),
            "claim vendor interface");
      claimed = true;
    } catch (...) {
      if (handle)
        libusb_close(handle);
      handle = nullptr;
      throw;
    }
  }
  ~Usb() override {
    if (handle) {
      if (claimed)
        libusb_release_interface(handle, interface_number);
      libusb_close(handle);
    }
  }
  void write_control(const Bytes &b) override {
    size_t used = 0;
    while (used < b.size()) {
      int actual = 0;
      int rc = libusb_bulk_transfer(
          handle, command_ep, const_cast<unsigned char *>(b.data() + used),
          int(b.size() - used), &actual, 5000);
      check(rc, "write command");
      if (actual <= 0)
        throw std::runtime_error("No USB write progress");
      used += size_t(actual);
    }
  }
  Bytes read_control(size_t n, unsigned t) override {
    return read(reply_ep, n, t);
  }
  Bytes read_iq(size_t n, unsigned t) override { return read(iq_ep, n, t); }
};
} // namespace
std::vector<DeviceInfo> enumerate_usb() {
  Context c;
  List list(c);
  std::vector<DeviceInfo> out;
  for (ssize_t i = 0; i < list.n; i++) {
    libusb_device_descriptor d{};
    if (libusb_get_device_descriptor(list.p[i], &d) < 0 || d.idVendor != vid ||
        d.idProduct != pid)
      continue;
    libusb_device_handle *h = nullptr;
    int rc = libusb_open(list.p[i], &h);
    if (rc < 0) {
      out.push_back(
          {"", "CMX918 inaccessible: " + std::string(libusb_error_name(rc))});
      continue;
    }
    try {
      auto s = serial_of(h, d.iSerialNumber);
      out.push_back({s, "CMX918 " + s});
    } catch (...) {
      libusb_close(h);
      throw;
    }
    libusb_close(h);
  }
  return out;
}
std::unique_ptr<Transport> open_usb(const std::string &serial) {
  return std::make_unique<Usb>(serial);
}
} // namespace cmx
