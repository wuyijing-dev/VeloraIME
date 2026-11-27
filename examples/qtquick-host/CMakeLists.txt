cmake_minimum_required(VERSION 3.25)
project(VeloraQuickHost LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)
find_package(Qt6 6.12 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(velora-quick-host main.cpp)
qt_add_resources(velora-quick-host host_view PREFIX /example FILES Main.qml)
target_link_libraries(velora-quick-host PRIVATE Qt6::Quick Qt6::QuickControls2)
target_compile_features(velora-quick-host PRIVATE cxx_std_17)
install(TARGETS velora-quick-host RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

set(VELORA_EXAMPLE_IMPORT_ROOT "" CACHE PATH "Installed QML import root used by the example tests")
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    qt_add_executable(velora-quick-host-check integration_test.cpp)
    qt_add_resources(velora-quick-host-check host_test_view PREFIX /example FILES Main.qml)
    target_link_libraries(velora-quick-host-check PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_features(velora-quick-host-check PRIVATE cxx_std_17)
    if(VELORA_EXAMPLE_IMPORT_ROOT)
        find_program(VELORA_EXAMPLE_QMLLINT qmllint HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
        add_test(NAME velora.installed_quick_host_qmllint COMMAND "${VELORA_EXAMPLE_QMLLINT}"
            -W 0 -I "${VELORA_EXAMPLE_IMPORT_ROOT}" "${CMAKE_CURRENT_SOURCE_DIR}/Main.qml")
        add_test(NAME velora.installed_quick_host COMMAND velora-quick-host-check)
        set_tests_properties(velora.installed_quick_host PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;VELORA_EXAMPLE_IMPORT_ROOT=${VELORA_EXAMPLE_IMPORT_ROOT}"
            TIMEOUT 45)
    endif()
endif()
