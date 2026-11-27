cmake_minimum_required(VERSION 3.25)
project(VeloraKeyboardOpenProbe LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.12)

set(host_example "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/qtquick-host")
qt_add_executable(velora-keyboard-open-probe main.cpp)
qt_add_resources(velora-keyboard-open-probe example_probe_qml PREFIX /example
    BASE "${host_example}" FILES "${host_example}/Main.qml")
qt_add_resources(velora-keyboard-open-probe frame_probe_qml PREFIX /probe FILES FrameDiagnostic.qml)
target_link_libraries(velora-keyboard-open-probe PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
target_compile_features(velora-keyboard-open-probe PRIVATE cxx_std_17)
