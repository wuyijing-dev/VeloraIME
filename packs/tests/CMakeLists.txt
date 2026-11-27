cmake_minimum_required(VERSION 3.22)
project(VeloraWordPackTests LANGUAGES CXX)

if(NOT TARGET Qt6::Test)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Concurrent Network Qml Test)
endif()
if(NOT TARGET OpenSSL::Crypto)
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

add_executable(velora_word_pack_tests
    pack_tests.cpp
    ../lexiconpackmanager.cpp
    ../lexiconpackmanager.h
    ../lexiconpackstore.cpp
    ../lexiconpackstore_p.h)
set_target_properties(velora_word_pack_tests PROPERTIES AUTOMOC ON)
target_compile_features(velora_word_pack_tests PRIVATE cxx_std_17)
target_link_libraries(velora_word_pack_tests PRIVATE
    Qt6::Concurrent Qt6::Network Qt6::Qml Qt6::Test OpenSSL::Crypto)

enable_testing()
add_test(NAME velora.word_packs
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py"
            "$<TARGET_FILE:velora_word_pack_tests>")
