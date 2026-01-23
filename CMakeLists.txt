cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0144 NEW)
cmake_policy(SET CMP0167 NEW)

include(FetchContent)

project(AntyCopyRightCppServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED json url headers)
find_package(OpenSSL REQUIRED)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

FetchContent_MakeAvailable(googletest)

add_subdirectory(test)

file(GLOB_RECURSE SRCS src/*.cpp)

# Exclude main.cpp for library
list(REMOVE_ITEM SRCS "${CMAKE_SOURCE_DIR}/src/main.cpp")
configure_file(config.hpp.in ${CMAKE_BINARY_DIR}/config.hpp @ONLY)

include_directories(${CMAKE_BINARY_DIR})

add_library(${PROJECT_NAME}_lib ${SRCS})

add_executable(${PROJECT_NAME} src/main.cpp)

target_link_libraries(${PROJECT_NAME}_lib PUBLIC
        "${TORCH_LIBRARIES}"
        Boost::headers
        Boost::json
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
)

target_link_libraries(${PROJECT_NAME} PUBLIC ${PROJECT_NAME}_lib)