find_package(Fcitx5ModuleTestFrontend REQUIRED)
find_package(Fcitx5ModuleTestIM REQUIRED)

# Addon-Konfiguration fuer Testumgebung bereitstellen
configure_file(
    "${CMAKE_SOURCE_DIR}/addon/vimotion.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/addon/vimotion.conf"
    @ONLY)
configure_file(
    "${CMAKE_SOURCE_DIR}/addon/vimotion-im.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/inputmethod/vimotion-im.conf"
    @ONLY)

add_executable(test_vimotion test_vimotion.cpp)
target_link_libraries(test_vimotion PRIVATE
    Fcitx5::Core
    Fcitx5::Utils
    Fcitx5::Module::TestFrontend
    Fcitx5::Module::TestIM
)
target_compile_definitions(test_vimotion PRIVATE
    TESTING_BINARY_DIR="${CMAKE_BINARY_DIR}"
    TESTING_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_test(NAME vimotion_test COMMAND test_vimotion)
