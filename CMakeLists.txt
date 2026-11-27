cmake_minimum_required(VERSION 3.25)
project(VeloraIME VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.12 REQUIRED COMPONENTS Concurrent Gui Network Qml Quick QuickControls2 ShaderTools Sql)
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
qt_standard_project_setup(REQUIRES 6.12)

set(VELORA_QML_INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/veloraime/qml"
    CACHE STRING "Installation directory for the VeloraIME QML import tree")
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")
set(velora_qml_files
    qml/GlassTheme.qml
    qml/GlassSurface.qml
    qml/InkPad.qml
    qml/KeyboardPanel.qml
    qml/Keycap.qml
    qml/KeyLayouts.js
)
foreach(qml_file IN LISTS velora_qml_files)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(VeloraIME SHARED)
qt_add_qml_module(VeloraIME
    URI Rk.TouchKeyboard
    VERSION 1.0
    PLUGIN_TARGET VeloraIMEPlugin
    QML_FILES ${velora_qml_files}
    SOURCES
        src/keyboardcontroller.cpp src/keyboardcontroller.h
        src/keyboardlexicon.cpp src/keyboardlexicon.h
        src/handwritingrecognizer.cpp src/handwritingrecognizer.h
        src/voicerecognizer.cpp src/voicerecognizer.h
        packs/lexiconpackmanager.cpp packs/lexiconpackmanager.h
        packs/lexiconpackstore.cpp packs/lexiconpackstore_p.h
)
target_include_directories(VeloraIME PRIVATE src packs)
target_link_libraries(VeloraIME PRIVATE Qt6::Concurrent Qt6::Gui Qt6::Network Qt6::Quick Qt6::Sql OpenSSL::Crypto)
qt_add_shaders(VeloraIME velora_glass_shader PREFIX /qt/qml/Rk/TouchKeyboard
    BASE qml FILES qml/shaders/GlassPanel.frag)
qt_add_resources(VeloraIME velora_lexicon
    PREFIX /velora
    BASE data/lexicon
    FILES
        data/lexicon/luna.dict.yaml
        data/lexicon/essay.txt
        data/lexicon/TSCharacters.txt
        data/lexicon/TSPhrases.txt
        data/lexicon/english.txt
)

qt_query_qml_module(VeloraIME TARGET_PATH velora_qml_target_path
    QMLDIR velora_qmldir TYPEINFO velora_qmltypes)
set(velora_qml_install_path "${VELORA_QML_INSTALL_DIR}/${velora_qml_target_path}")
set_target_properties(VeloraIMEPlugin PROPERTIES INSTALL_RPATH "$ORIGIN")
install(TARGETS VeloraIME VeloraIMEPlugin LIBRARY DESTINATION "${velora_qml_install_path}")
install(FILES "${velora_qmldir}" "${velora_qmltypes}" DESTINATION "${velora_qml_install_path}")
install(DIRECTORY data/lexicon/ DESTINATION "${CMAKE_INSTALL_DATADIR}/veloraime/licenses"
    FILES_MATCHING PATTERN "LICENSE.*" PATTERN "README.md")

option(VELORA_BUILD_WESTON "Build the Weston compositor input method client" OFF)
if(VELORA_BUILD_WESTON)
    add_subdirectory(system)
endif()

option(VELORA_BUILD_DEMO "Build the VeloraIME input demo" ON)
if(VELORA_BUILD_DEMO)
    qt_add_executable(veloraime-demo demo/main.cpp)
    qt_add_resources(veloraime-demo velora_demo PREFIX /demo BASE demo
        FILES demo/Main.qml demo/cloud-production-1.pub.pem)
    qt_add_resources(veloraime-demo velora_demo_fonts PREFIX /demo/fonts BASE assets/fonts
        FILES assets/fonts/Nunito.ttf)
    target_link_libraries(veloraime-demo PRIVATE Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(veloraime-demo PRIVATE
        VELORA_QML_INSTALL_DIR="${VELORA_QML_INSTALL_DIR}"
    )
    add_dependencies(veloraime-demo VeloraIMEPlugin)
    install(TARGETS veloraime-demo RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(FILES assets/fonts/LICENSE.Nunito DESTINATION "${CMAKE_INSTALL_DATADIR}/veloraime/licenses")
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME veloraime-demo-smoke COMMAND veloraime-demo --smoke-test)
        set_tests_properties(veloraime-demo-smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    add_subdirectory(packs/tests)

    qt_add_executable(velora-engine-tests tests/engine_integration.cpp)
    target_include_directories(velora-engine-tests PRIVATE src)
    target_link_libraries(velora-engine-tests PRIVATE VeloraIME Qt6::Quick Qt6::Sql Qt6::Test)
    add_test(NAME velora.engine COMMAND velora-engine-tests)

    find_program(VELORA_QMLTESTRUNNER qmltestrunner HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    add_test(NAME velora.qml_panel COMMAND "${VELORA_QMLTESTRUNNER}"
        -import "${QT_QML_OUTPUT_DIRECTORY}" -input "${CMAKE_CURRENT_SOURCE_DIR}/tests/qml")
    set_tests_properties(velora.engine velora.qml_panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endif()
