cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(AntyCopyRightCppServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(Boost REQUIRED json thread url headers)
add_definitions(-DBOOST_HANA_CONFIG_ENABLE_STRING_UDL)
find_package(OpenSSL REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED libpqxx)
pkg_check_modules(POPPLER_CPP REQUIRED IMPORTED_TARGET poppler-cpp)

set(boost_system_DIR "${CMAKE_BINARY_DIR}/dummy_boost_system")
file(MAKE_DIRECTORY "${boost_system_DIR}")

file(WRITE "${boost_system_DIR}/boost_systemConfig.cmake" "
    if(NOT TARGET Boost::system)
        add_library(Boost::system INTERFACE IMPORTED)
        target_link_libraries(Boost::system INTERFACE Boost::headers)
    endif()
")

file(WRITE "${boost_system_DIR}/boost_systemConfigVersion.cmake" "
    set(PACKAGE_VERSION \"\${PACKAGE_FIND_VERSION}\")
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    set(PACKAGE_VERSION_EXACT TRUE)
")

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

FetchContent_Declare(
        cryptopp_cmake
        GIT_REPOSITORY https://github.com/abdes/cryptopp-cmake.git
        GIT_TAG CRYPTOPP_8_9_0
)

FetchContent_Declare(
        ozo
        GIT_REPOSITORY https://github.com/yandex/ozo.git
        GIT_TAG v0.1.0
)

set(MZ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(MZ_PROJECT OFF CACHE BOOL "" FORCE)

set(CRYPTOPP_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CRYPTOPP_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest pugixml minizip-ng cryptopp_cmake ozo)

file(GLOB_RECURSE SRCS CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SRCS "${CMAKE_SOURCE_DIR}/src/main.cpp")

configure_file(config.hpp.in ${CMAKE_BINARY_DIR}/config.hpp @ONLY)

add_library(${PROJECT_NAME}_lib ${SRCS} ${PB_CPP} ${GRPC_CPP})

target_include_directories(${PROJECT_NAME}_lib
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>
        PRIVATE
            ${PQXX_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}_lib PUBLIC
        Boost::headers
        Boost::json
        Boost::thread
        Boost::url
        OpenSSL::SSL
        OpenSSL::Crypto
        pugixml
        minizip
        ${PQXX_LIBRARIES}
        ICU::uc
        PkgConfig::POPPLER_CPP
        cryptopp
        ozo
)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
