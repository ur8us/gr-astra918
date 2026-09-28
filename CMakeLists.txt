cmake_minimum_required(VERSION 3.18)
project(gr-astra918 VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CTest)
include(CMakePackageConfigHelpers)
find_package(Gnuradio 3.10 REQUIRED)
find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
endif()
if(TARGET PkgConfig::LIBUSB)
  set(ASTRA_USB PkgConfig::LIBUSB)
else()
  find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES libusb-1.0)
  find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
  if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "libusb-1.0 headers and library are required")
  endif()
  add_library(astra_usb UNKNOWN IMPORTED)
  set_target_properties(astra_usb PROPERTIES IMPORTED_LOCATION "${LIBUSB_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${LIBUSB_INCLUDE_DIR}")
  set(ASTRA_USB astra_usb)
endif()
add_library(gnuradio-astra918 SHARED
  lib/source.cc lib/control.cc
  lib/vendor/cmx918_protocol.cpp lib/vendor/cmx918_device.cpp lib/vendor/usb_transport.cpp)
target_compile_features(gnuradio-astra918 PUBLIC cxx_std_17)
target_include_directories(gnuradio-astra918 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE lib/vendor)
target_link_libraries(gnuradio-astra918 PUBLIC gnuradio::gnuradio-runtime PRIVATE ${ASTRA_USB})
if(WIN32)
  target_compile_definitions(gnuradio-astra918 PRIVATE NOMINMAX)
endif()
set_target_properties(gnuradio-astra918 PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0)
pybind11_add_module(astra918_python python/bindings.cc)
target_link_libraries(astra918_python PRIVATE gnuradio-astra918)
set_target_properties(astra918_python PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/gnuradio/astra918"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python/gnuradio/astra918")
file(COPY python/gnuradio/astra918/__init__.py DESTINATION "${CMAKE_BINARY_DIR}/python/gnuradio/astra918")
set(ASTRA_PYTHON_DIR "${GR_PYTHON_DIR}/gnuradio/astra918")
install(TARGETS gnuradio-astra918 EXPORT gnuradio-astra918-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/gnuradio/astra918 DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio)
install(TARGETS astra918_python LIBRARY DESTINATION ${ASTRA_PYTHON_DIR} RUNTIME DESTINATION ${ASTRA_PYTHON_DIR})
install(FILES python/gnuradio/astra918/__init__.py DESTINATION ${ASTRA_PYTHON_DIR})
install(DIRECTORY grc/ DESTINATION ${CMAKE_INSTALL_DATADIR}/gnuradio/grc/blocks FILES_MATCHING PATTERN "*.block.yml")
set(ASTRA_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/gnuradio-astra918")
configure_package_config_file(cmake/gnuradio-astra918Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/gnuradio-astra918Config.cmake"
  INSTALL_DESTINATION "${ASTRA_CMAKE_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/gnuradio-astra918ConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(EXPORT gnuradio-astra918-targets NAMESPACE gr:: FILE gnuradio-astra918Targets.cmake
  DESTINATION "${ASTRA_CMAKE_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/gnuradio-astra918Config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/gnuradio-astra918ConfigVersion.cmake"
  DESTINATION "${ASTRA_CMAKE_DIR}")
if(BUILD_TESTING)
  add_executable(astra918_protocol_test tests/protocol.cc)
  target_include_directories(astra918_protocol_test PRIVATE lib/vendor)
  target_link_libraries(astra918_protocol_test PRIVATE gnuradio-astra918)
  add_test(NAME protocol COMMAND astra918_protocol_test)
endif()
