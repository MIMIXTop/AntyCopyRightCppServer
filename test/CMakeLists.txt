file(GLOB_RECURSE TEST_SOURCES "src/*.cpp")

add_executable(RunAllTests ${TEST_SOURCES})

target_include_directories(RunAllTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_compile_definitions(RunAllTests PRIVATE
    TEST_DOC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/doc"
    TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/output"
)

target_link_libraries(RunAllTests PRIVATE 
    GTest::gtest
    GTest::gtest_main
    AntyCopyRightCppServer_lib
)
