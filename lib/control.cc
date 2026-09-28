#include <gnuradio/astra918/control.h>
#include <gnuradio/io_signature.h>
#include <stdexcept>

namespace gr::astra918 {
class control_impl final : public control {
    source::sptr receiver_;
public:
    explicit control_impl(source::sptr receiver)
        : gr::block("astra918_control", gr::io_signature::make(0, 0, 0),
                    gr::io_signature::make(0, 0, 0)), receiver_(std::move(receiver)) {
        if (!receiver_) throw std::invalid_argument("Astra918 control needs its source block");
        message_port_register_in(pmt::intern("cmd"));
        message_port_register_out(pmt::intern("status"));
        set_msg_handler(pmt::intern("cmd"), [this](pmt::pmt_t msg) {
            try { receiver_->command(msg); }
            catch (const std::exception& e) {
                auto d = receiver_->state();
                message_port_pub(pmt::intern("status"), pmt::dict_add(
                    d, pmt::intern("command_error"), pmt::intern(e.what())));
                return;
            }
            message_port_pub(pmt::intern("status"), receiver_->state());
        });
    }
    int general_work(int, gr_vector_int&, gr_vector_const_void_star&, gr_vector_void_star&) override {
        return 0;
    }
};
control::sptr control::make(source::sptr receiver) {
    return gnuradio::make_block_sptr<control_impl>(std::move(receiver));
}
} // namespace gr::astra918
