enable_language(C)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client>=1.21 xkbcommon)
pkg_get_variable(wayland_protocols_dir wayland-protocols pkgdatadir)
find_program(VELORA_WAYLAND_SCANNER wayland-scanner REQUIRED)

set(VELORA_WAYLAND_PROTOCOLS_DIR "${wayland_protocols_dir}" CACHE PATH "wayland-protocols data directory")
set(protocol_files)
foreach(protocol IN ITEMS input-method text-input)
    set(xml "${VELORA_WAYLAND_PROTOCOLS_DIR}/unstable/${protocol}/${protocol}-unstable-v1.xml")
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client.h")
    set(code "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c")
    add_custom_command(OUTPUT "${header}" "${code}"
        COMMAND "${VELORA_WAYLAND_SCANNER}" client-header "${xml}" "${header}"
        COMMAND "${VELORA_WAYLAND_SCANNER}" private-code "${xml}" "${code}"
        DEPENDS "${xml}" VERBATIM)
    list(APPEND protocol_files "${header}" "${code}")
endforeach()

qt_add_executable(veloraime-weston main.cpp waylandkeyboard.cpp waylandkeyboard.h ${protocol_files})
target_include_directories(veloraime-weston PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" ../src)
target_link_libraries(veloraime-weston PRIVATE VeloraIME PkgConfig::WAYLAND Qt6::Quick Qt6::Sql)
target_compile_definitions(veloraime-weston PRIVATE
    VELORA_BUILD_IMPORT_DIR="${QT_QML_OUTPUT_DIRECTORY}"
    VELORA_QML_INSTALL_DIR="${VELORA_QML_INSTALL_DIR}")
if(IS_ABSOLUTE "${VELORA_QML_INSTALL_DIR}")
    set(weston_module_runpath "${velora_qml_install_path}")
else()
    set(weston_module_runpath "$ORIGIN/../${velora_qml_install_path}")
endif()
set_target_properties(veloraime-weston PROPERTIES INSTALL_RPATH "${weston_module_runpath}")
add_dependencies(veloraime-weston VeloraIMEPlugin)
install(TARGETS veloraime-weston RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
