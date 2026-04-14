find_package(Fcitx5ModuleTestFrontend QUIET)
find_package(Fcitx5ModuleTestIM QUIET)

if(NOT Fcitx5ModuleTestFrontend_FOUND OR NOT Fcitx5ModuleTestIM_FOUND)
    message(STATUS
        "vimotion: fcitx5 test frontends not found — skipping tests.")
    return()
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/src/vimotion.conf.in"
    "${CMAKE_CURRENT_BINARY_DIR}/addon/vimotion.conf"
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
