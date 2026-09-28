#pragma once
#include <gnuradio/block.h>
#include <gnuradio/astra918/source.h>
#include <memory>

namespace gr::astra918 {
class control : virtual public gr::block {
public:
    using sptr = std::shared_ptr<control>;
    static sptr make(source::sptr receiver);
};
} // namespace gr::astra918
