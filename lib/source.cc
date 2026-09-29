#include <gnuradio/astra918/source.h>
#include <gnuradio/gr_complex.h>
#include <gnuradio/io_signature.h>
#include "cmx918_device.h"
#include "astra_stream.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace gr::astra918 {
namespace {
using Clock = std::chrono::steady_clock;
constexpr int64_t min_hz = 70000, max_hz = 260000000;
pmt::pmt_t key(const char* name) { return pmt::intern(name); }
void add(pmt::pmt_t& d, const char* name, uint64_t value) {
    d = pmt::dict_add(d, key(name), pmt::from_uint64(value));
}
void add_signed(pmt::pmt_t& d, const char* name, int64_t value) {
    d = pmt::dict_add(d, key(name), pmt::from_long(value));
}
pmt::pmt_t encode(const cmx::State& s) {
    auto d = pmt::make_dict();
    add(d, "center_hz", s.center);
    add(d, "dial_hz", s.requested);
    add_signed(d, "audio_offset", s.offset);
    add(d, "mode", s.audio_mode);
    add(d, "audio_low", s.audio_low);
    add(d, "audio_high", s.audio_high);
    add(d, "reference", s.reference_clock);
    add(d, "gpio", s.gpio);
    add(d, "input", s.input);
    add(d, "rf_auto", s.rf_mode == 0);
    add(d, "if_auto", s.if_mode == 0);
    add(d, "rf_gain", s.rf_gain);
    add(d, "if_gain", s.if_gain);
    add(d, "lf_gain", s.lf_gain);
    add(d, "attenuator", s.lf_attenuator);
    add(d, "capacitor", s.lf_mf_capacitor);
    add(d, "revision", s.revision);
    add(d, "saved_revision", s.saved_revision);
    add(d, "generation", s.generation);
    add(d, "configured", s.configured);
    add(d, "streaming", s.streaming);
    add(d, "error", s.error);
    return d;
}
int64_t number(pmt::pmt_t d, const char* name) {
    auto p = pmt::dict_ref(d, key(name), pmt::PMT_NIL);
    if (p == pmt::PMT_NIL || !pmt::is_integer(p))
        throw std::invalid_argument(std::string("Missing integer ") + name);
    return pmt::to_long(p);
}
void range(int64_t n, int64_t low, int64_t high) {
    if (n < low || n > high) throw std::out_of_range("Astra918 control outside range");
}
} // namespace

class source_impl final : public source {
    const std::string serial_;
    mutable std::mutex mutex_;
    std::unique_ptr<cmx::Device> device_;
    bool running_ = false;
    cmx::State current_;
    std::string transport_error_;
    cmx::AstraDecoder decoder_;
    std::deque<cmx::IqBlock> pending_;
    size_t sample_ = 0;
    Clock::time_point poll_{};
    pmt::pmt_t status_port_ = key("status");
    pmt::pmt_t cmd_port_ = key("cmd");
    mutable std::mutex status_mutex_;
    pmt::pmt_t cached_state_ = pmt::make_dict();

    pmt::pmt_t snapshot() const {
        auto d = encode(current_);
        if (!transport_error_.empty())
            d = pmt::dict_add(d, key("transport_error"), pmt::intern(transport_error_));
        return d;
    }
    pmt::pmt_t cache_state() {
        auto d = snapshot();
        std::lock_guard<std::mutex> lock(status_mutex_);
        cached_state_ = d;
        return d;
    }
    void publish() { message_port_pub(status_port_, cache_state()); }
    bool connect_device() {
        try {
            auto candidate = std::make_unique<cmx::Device>(cmx::open_usb(serial_));
            if (candidate->state.configured) candidate->start();
            device_ = std::move(candidate);
            current_ = device_->state;
            transport_error_.clear();
            decoder_ = cmx::AstraDecoder();
            pending_.clear(); sample_ = 0;
            poll_ = Clock::now();
            publish();
            return true;
        } catch (const std::exception& e) {
            device_.reset();
            current_ = cmx::State{};
            transport_error_ = e.what();
            pending_.clear(); sample_ = 0;
            publish();
            return false;
        }
    }
    void refresh() {
        auto before = current_;
        current_ = device_->refresh();
        if (current_.generation != before.generation || !current_.configured) {
            pending_.clear(); sample_ = 0;
            decoder_ = cmx::AstraDecoder();
        }
        const bool changed = current_.revision != before.revision ||
            current_.generation != before.generation ||
            current_.configured != before.configured || current_.error != before.error;
        auto d = cache_state();
        if (changed) message_port_pub(status_port_, d);
    }
    void apply(pmt::pmt_t d) {
        if (!pmt::is_dict(d)) throw std::invalid_argument("Astra918 command must be a PMT dict");
        auto name = pmt::dict_ref(d, key("name"), pmt::PMT_NIL);
        if (!pmt::is_symbol(name)) throw std::invalid_argument("Astra918 command needs name symbol");
        const std::string n = pmt::symbol_to_string(name);
        if (n == "save") device_->command(cmx::Save);
        else if (n == "retry") device_->command(cmx::Retry);
        else {
            int64_t value = number(d, "value");
            if (n == "center_hz") {
                range(value, min_hz, max_hz);
                const auto dial = value + current_.offset;
                range(dial, min_hz, max_hz);
                device_->command(cmx::SetFrequency, cmx::integer(uint64_t(dial), 8));
            } else if (n == "audio_offset") {
                range(value, -59500, 59500);
                const auto dial = int64_t(current_.center) + value;
                range(dial, min_hz, max_hz);
                device_->command(cmx::TuneChannel, cmx::integer(uint64_t(dial), 8));
            } else if (n == "mode") {
                range(value, 1, 2);
                device_->command(cmx::SetAudioMode, {uint8_t(value)});
            } else if (n == "audio_filter") {
                const int64_t high = number(d, "high");
                range(value, 0, 4999); range(high, value + 1, 5000);
                auto p = cmx::integer(value, 2), h = cmx::integer(high, 2);
                p.insert(p.end(), h.begin(), h.end());
                device_->command(cmx::SetAudioFilter, p);
            } else if (n == "reference") {
                if (!device_->capabilities.reference_clock) throw std::runtime_error("Reference selection unsupported");
                range(value, 0, 1);
                device_->command(cmx::SetReference, {uint8_t(value)});
            } else if (n.size() == 5 && n.substr(0, 4) == "gpio" && n[4] >= '0' && n[4] <= '7') {
                if (!device_->capabilities.logical_gpio) throw std::runtime_error("Logical GPIO unsupported");
                range(value, 0, 1);
                uint8_t mask = uint8_t(1u << (n[4] - '0'));
                device_->command(cmx::UpdateGpio, {mask, uint8_t(value ? mask : 0)});
            } else if (n == "input") {
                range(value, 0, 3);
                device_->command(cmx::SetInput, {uint8_t(value)});
            } else if (n == "rf_auto" || n == "if_auto") {
                range(value, 0, 1);
                device_->command(cmx::SetGainMode, {uint8_t(n == "if_auto"), uint8_t(!value)});
            } else if (n == "rf_gain" || n == "if_gain" || n == "lf_gain" || n == "attenuator") {
                uint8_t block = n == "rf_gain" ? 0 : n == "if_gain" ? 1 : n == "lf_gain" ? 2 : 3;
                range(value, 0, block == 0 ? 38 : block == 1 ? 31 : 15);
                device_->command(cmx::SetGain, {block, uint8_t(value)});
            } else if (n == "capacitor") {
                range(value, 0, 4095);
                device_->command(cmx::SetLfMfCapacitor, cmx::integer(value, 2));
            } else throw std::invalid_argument("Unknown Astra918 setting: " + n);
        }
        refresh();
        if (current_.configured && !current_.streaming) {
            device_->start();
            current_ = device_->state;
            publish();
        }
    }
public:
    explicit source_impl(std::string serial)
        : gr::sync_block("astra918_source", gr::io_signature::make(0, 0, 0),
                         gr::io_signature::make(1, 1, sizeof(gr_complex))),
          serial_(std::move(serial)) {
        cache_state();
        message_port_register_out(status_port_);
        message_port_register_in(cmd_port_);
        set_msg_handler(cmd_port_, [this](pmt::pmt_t msg) {
            try { command(msg); }
            catch (const std::exception& e) {
                auto d = state();
                message_port_pub(status_port_, pmt::dict_add(d, key("command_error"), pmt::intern(e.what())));
            }
        });
    }
    bool start() override {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = true;
        connect_device();
        return true;
    }
    bool stop() override {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        if (device_) {
            device_->stop();
            current_ = device_->state;
            device_.reset();
        }
        pending_.clear(); sample_ = 0;
        cache_state();
        return true;
    }
    double center_freq() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return double(current_.center);
    }
    pmt::pmt_t state() const override {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return cached_state_;
    }
    void command(pmt::pmt_t d) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) throw std::runtime_error("Astra918 source is stopped");
        if (!device_) {
            auto name = pmt::is_dict(d) ? pmt::dict_ref(d, key("name"), pmt::PMT_NIL) : pmt::PMT_NIL;
            if (pmt::is_symbol(name) && pmt::symbol_to_string(name) == "retry") {
                if (!connect_device()) throw std::runtime_error(transport_error_);
                return;
            }
            throw std::runtime_error("Astra918 receiver unavailable; use Retry receiver");
        }
        try { apply(d); }
        catch (...) { refresh(); throw; }
    }
    int work(int noutput_items, gr_vector_const_void_star&, gr_vector_void_star& output_items) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return -1;
        if (!device_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return 0;
        }
        if (Clock::now() - poll_ >= std::chrono::milliseconds(200)) {
            refresh(); poll_ = Clock::now();
        }
        auto* out = static_cast<gr_complex*>(output_items[0]);
        int written = 0;
        while (written < noutput_items) {
            if (!current_.configured || !current_.streaming) break;
            if (pending_.empty()) {
                for (auto& frame : decoder_.feed(device_->read_iq())) {
                    if (frame.generation == current_.generation && frame.center == current_.center)
                        pending_.push_back(std::move(frame));
                }
                if (pending_.empty()) break;
            }
            auto& block = pending_.front();
            if (sample_ == 0) {
                add_item_tag(0, nitems_written(0) + written, key("rx_freq"), pmt::from_uint64(block.center));
                add_item_tag(0, nitems_written(0) + written, key("generation"), pmt::from_uint64(block.generation));
            }
            size_t take = std::min<size_t>(noutput_items - written, block.samples.size() - sample_);
            std::copy_n(block.samples.begin() + sample_, take, out + written);
            written += int(take); sample_ += take;
            if (sample_ == block.samples.size()) { pending_.pop_front(); sample_ = 0; }
        }
        if (written == 0 && (!current_.configured || !current_.streaming))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return written;
    }
};
source::sptr source::make(const std::string& serial) {
    return gnuradio::make_block_sptr<source_impl>(serial);
}
} // namespace gr::astra918
