#pragma once
#include <gnuradio/sync_block.h>
#include <pmt/pmt.h>
#include <memory>
#include <string>

namespace gr::astra918 {
class source : virtual public gr::sync_block {
public:
    using sptr = std::shared_ptr<source>;
    static sptr make(const std::string& serial = "");
    virtual double center_freq() const = 0;
    virtual pmt::pmt_t state() const = 0;
    virtual void command(pmt::pmt_t message) = 0;
};
} // namespace gr::astra918
