cmake_minimum_required (VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_FLAGS "-std=c++2b -stdlib=libc++ -O2 -ggdb")

project (chatServer)

find_package(Boost 1.81.0 REQUIRED)
find_package(spdlog REQUIRED)

add_executable(server "src/server.cpp")
add_executable(client "src/client.cpp")

# target_compile_options(server PUBLIC -DBOOST_ASIO_ENABLE_HANDLER_TRACKING)
target_compile_options(client PUBLIC )

target_precompile_headers(server PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)
target_precompile_headers(client PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)

target_link_libraries(server pthread spdlog::spdlog_header_only ssl crypto)
target_link_libraries(client pthread spdlog::spdlog_header_only ssl crypto)
