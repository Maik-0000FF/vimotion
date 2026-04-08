add_library(vimotion MODULE
    src/vimotion.cpp
    src/motions.cpp
)

target_link_libraries(vimotion PRIVATE Fcitx5::Core)
target_include_directories(vimotion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(vimotion PROPERTIES PREFIX "")

configure_file(vimotion.conf.in vimotion.conf @ONLY)
configure_file(vimotion-im.conf.in vimotion-im.conf @ONLY)

install(TARGETS vimotion DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vimotion.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vimotion-im.conf"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")
