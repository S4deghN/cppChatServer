cmake_minimum_required (VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_FLAGS "-std=c++2b -stdlib=libc++ -O2 -ggdb -DSPDLOG_COMPILED_LIB")

project(chatServer)

find_package(Boost REQUIRED)

include_directories(SYSTEM spdlog/include)
file(GLOB spdlog_srcs spdlog/src/*.cpp)
add_library(spdlog_objs OBJECT ${spdlog_srcs})

# add_subdirectory(spdlog)
add_executable(server "src/server.cpp")
add_executable(client "src/client.cpp")

# target_compile_options(server PUBLIC -DBOOST_ASIO_ENABLE_HANDLER_TRACKING)
# target_compile_options(client PUBLIC )

target_precompile_headers(server PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)
target_precompile_headers(client PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)

target_link_libraries(server pthread ssl crypto $<TARGET_OBJECTS:spdlog_objs>)
target_link_libraries(client pthread ssl crypto $<TARGET_OBJECTS:spdlog_objs>)
