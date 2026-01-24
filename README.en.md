[Русская версия](README.md)

# AntyCopyRightCppServer

A simple HTTPS server in C++ for handling requests related to educational courses and students.

## Description

This project is an asynchronous HTTPS server built using Boost.Beast and Boost.Asio. The server handles various types of requests such as getting course lists, assignments, student lists, and downloading submissions.

## Features

- Asynchronous HTTP/HTTPS request processing
- SSL/TLS encryption support
- Request handling for courses, students, and assignments
- Configurable via config.hpp

## Requirements

- C++23
- CMake 3.20+
- Boost (with json, stacktrace_addr2line, url, headers components)
- OpenSSL
- Google Test (for testing)

## Building

1. Clone the repository:
   ```bash
   git clone <repository-url>
   cd AntyCopyRightCppServer
   ```

2. Create build directory:
   ```bash
   mkdir build
   cd build
   ```

3. Configure with CMake:
   ```bash
   cmake ..
   ```

4. Build the project:
   ```bash
   make
   ```

## Running

After building, run the server:
```bash
./AntyCopyRightCppServer
```

The server will listen on `127.0.0.1:8080` using SSL certificates `../server.crt` and `../server.key`.

## Testing

To run tests:
```bash
cd build
ctest
```

Or directly:
```bash
./test/RunAllTests
```

## Project Structure

- `src/` - server source code
- `test/` - unit tests
- `build/` - build directory
- `CMakeLists.txt` - CMake configuration
- `config.hpp.in` - configuration template

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.