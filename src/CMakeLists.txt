add_library(vimotion MODULE
    vimotion.cpp
    motions.cpp
)

target_link_libraries(vimotion PRIVATE Fcitx5::Core Fcitx5::Utils)
target_include_directories(vimotion PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_options(vimotion PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(vimotion PROPERTIES PREFIX "")

configure_file(vimotion.conf.in vimotion.conf @ONLY)

install(TARGETS vimotion DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vimotion.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
