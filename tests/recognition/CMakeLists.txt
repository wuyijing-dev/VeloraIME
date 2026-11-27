cmake_minimum_required(VERSION 3.25)
project(VeloraRecognitionProbes LANGUAGES CXX)

find_package(Qt6 6.12 REQUIRED COMPONENTS Concurrent Qml)
set(CMAKE_AUTOMOC ON)

function(add_recognition_probe target source implementation)
    add_executable(${target} ${source} ../../src/${implementation}.cpp
        ../../src/${implementation}.h)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_include_directories(${target} PRIVATE ../../src)
    target_link_libraries(${target} PRIVATE Qt6::Concurrent Qt6::Qml)
endfunction()

add_recognition_probe(velora-handwriting-probe ../handwriting_probe.cpp handwritingrecognizer)
add_recognition_probe(velora-voice-fixture-probe voice_fixture_probe.cpp voicerecognizer)
