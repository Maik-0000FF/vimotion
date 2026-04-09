find_package(Fcitx5ModuleTestFrontend REQUIRED)
find_package(Fcitx5ModuleTestIM REQUIRED)

# Module-Descriptor fuer Testumgebung bereitstellen
configure_file(
    "${CMAKE_SOURCE_DIR}/module/vimotion-module.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/addon/vimotion-module.conf"
    @ONLY)

add_executable(test_vimotion_module test_vimotion_module.cpp)
target_link_libraries(test_vimotion_module PRIVATE
    Fcitx5::Core
    Fcitx5::Utils
    Fcitx5::Module::TestFrontend
    Fcitx5::Module::TestIM
)
target_compile_definitions(test_vimotion_module PRIVATE
    TESTING_BINARY_DIR="${CMAKE_BINARY_DIR}"
    TESTING_DATA_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_test(NAME vimotion_module_test COMMAND test_vimotion_module)
