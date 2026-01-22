file(GLOB_RECURSE TEST_SOURCES "src/*.cpp")

add_executable(RunAllTests ${TEST_SOURCES})

target_include_directories(RunAllTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(RunAllTests PRIVATE 
    GTest::gtest
    GTest::gtest_main
    AntyCopyRightCppServer_lib
)
