cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0144 NEW)
cmake_policy(SET CMP0167 NEW)

project(AntyCopyRightCppServer)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SRCS src/*.cpp)

find_package(Boost REQUIRED json)

add_executable(${PROJECT_NAME} ${SRCS})

find_package(OpenSSL REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC
        "${TORCH_LIBRARIES}"
        Boost::headers
        Boost::json
        OpenSSL::SSL
        OpenSSL::Crypto
)