cmake_minimum_required(VERSION 3.20)
project(VeloraTouchRuntime LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12)
qt_add_executable(velora-touch-runtime main.cpp)
qt_add_resources(velora-touch-runtime probe_qml PREFIX /probe FILES Probe.qml)
target_link_libraries(velora-touch-runtime PRIVATE Qt6::Quick Qt6::QuickControls2)
target_compile_features(velora-touch-runtime PRIVATE cxx_std_17)
