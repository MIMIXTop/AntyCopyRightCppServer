cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0144 NEW)
cmake_policy(SET CMP0167 NEW)

project(AntyCopyRightCppServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(Boost REQUIRED json thread url headers)
find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
)
FetchContent_Declare(
        pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml.git
        GIT_TAG v1.15
)

FetchContent_Declare(
        minizip-ng
        GIT_REPOSITORY https://github.com/zlib-ng/minizip-ng.git
        GIT_TAG 4.0.10
)

set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_PROJECT OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest pugixml minizip-ng)

file(GLOB_RECURSE SRCS src/*.cpp)
list(REMOVE_ITEM SRCS "${CMAKE_SOURCE_DIR}/src/main.cpp")

configure_file(config.hpp.in ${CMAKE_BINARY_DIR}/config.hpp @ONLY)

add_library(${PROJECT_NAME}_lib ${SRCS} ${PB_CPP} ${GRPC_CPP})

target_include_directories(${PROJECT_NAME}_lib PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
)

# Линкуем все зависимости к библиотеке
target_link_libraries(${PROJECT_NAME}_lib PUBLIC
        Boost::headers
        Boost::json
        Boost::thread
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
        pugixml
        minizip
)

# 6. СОЗДАНИЕ ИСПОЛНЯЕМОГО ФАЙЛА
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

# 7. ПОДКЛЮЧЕНИЕ ТЕСТОВ (ОБЯЗАТЕЛЬНО В КОНЦЕ!)
# Теперь папка test/ "видит" таргет ${PROJECT_NAME}_lib и может с ним слинковаться
if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()