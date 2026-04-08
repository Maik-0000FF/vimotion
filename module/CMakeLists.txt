add_library(vimotion-module MODULE
    src/vimotion_module.cpp
    ../addon/src/motions.cpp
)

target_link_libraries(vimotion-module PRIVATE Fcitx5::Core)
target_include_directories(vimotion-module PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/addon/src
)
set_target_properties(vimotion-module PROPERTIES PREFIX "")

configure_file(vimotion-module.conf.in vimotion-module.conf @ONLY)

install(TARGETS vimotion-module DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vimotion-module.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
