cmake_minimum_required (VERSION 3.10)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_CXX_FLAGS "-std=c++2b -stdlib=libc++ -fcoroutines-ts -O3")
set(CMAKE_LINK_LIBRARY_FLAG "-lc++ -lc++abi -lm -lc -lgcc_s -lgcc")

project (chatServer)

find_package(Boost REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)

add_executable(server "src/server1.cpp")
add_executable(client "src/client.cpp")

target_compile_options(server PUBLIC )
target_compile_options(client PUBLIC )

# target_link_options(server PUBLIC -lc++ -lc++abi -lm -lc -lgcc_s -lgcc)
# target_link_options(client PUBLIC -lc++ -lc++abi -lm -lc -lgcc_s -lgcc)

target_precompile_headers(server PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)
target_precompile_headers(client PUBLIC ${Boost_INCLUDE_DIR}/boost/asio.hpp)

target_link_libraries(server pthread spdlog::spdlog ssl crypto ${Boost_LIBRARIES})
target_link_libraries(client pthread spdlog::spdlog ssl crypto ${Boost_LIBRARIES})
