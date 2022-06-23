cmake_minimum_required (VERSION 3.10)

set(CMAKE_C_COMPILER "clang")
set(CMAKE_CXX_COMPILER "clang++")

project (chatServer)

find_package(Boost REQUIRED)
find_package(spdlog REQUIRED)

# if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
#   set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# endif ()

add_executable(server "src/server.cpp")
add_executable(client "src/client.cpp")

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(server PUBLIC -std=c++20 -stdlib=libc++ -fcoroutines-ts)
  target_compile_options(client PUBLIC -std=c++20 -stdlib=libc++ -fcoroutines-ts)
  target_link_options(server PUBLIC -lc++ -lc++abi -lm -lc -lgcc_s -lgcc)
  target_link_options(client PUBLIC -lc++ -lc++abi -lm -lc -lgcc_s -lgcc)
endif ()

#if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
#  target_compile_options(server PUBLIC -std=c++20 -fcoroutines)
#  target_compile_options(client PUBLIC -std=c++20 -fcoroutines)
#endif ()

target_link_libraries(server spdlog::spdlog_header_only pthread ${BOOST_LIBRARIES})
target_link_libraries(client spdlog::spdlog_header_only pthread ${BOOST_LIBRARIES})
