#include <gnuradio/astra918/source.h>
#include <gnuradio/astra918/control.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;
PYBIND11_MODULE(astra918_python, m) {
    py::module::import("gnuradio.gr");
    py::class_<gr::astra918::source, gr::sync_block,
               std::shared_ptr<gr::astra918::source>>(m, "source")
        .def_static("make", &gr::astra918::source::make, py::arg("serial") = "")
        .def("center_freq", &gr::astra918::source::center_freq);
    py::class_<gr::astra918::control, gr::block,
               std::shared_ptr<gr::astra918::control>>(m, "control")
        .def_static("make", &gr::astra918::control::make, py::arg("receiver"));
}
