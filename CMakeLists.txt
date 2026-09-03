cmake_minimum_required(VERSION 3.16)
project(shim_sandbox LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sibling wasigoc (GitHub: goxxlang/wasigoc). "go++" is this workspace's
# active fork of it (see go++/FORK.md -- never edit WASIGo++ directly from
# that tree), checked before the frozen historical "WASIGo++" checkout so
# a real fork-only feature (e.g. wasigo::gocvm) is actually visible here
# instead of silently falling back to the stale upstream runtime.hpp.
set(W2G_WASIGO_DIR "")
foreach(_cand IN ITEMS
    "${CMAKE_CURRENT_SOURCE_DIR}/../wasigoc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../go++"
    "${CMAKE_CURRENT_SOURCE_DIR}/../WASIGo++")
  if(EXISTS "${_cand}/src/runtime.hpp")
    set(W2G_WASIGO_DIR "${_cand}")
    break()
  endif()
endforeach()
if(W2G_WASIGO_DIR STREQUAL "")
  message(FATAL_ERROR "shim_sandbox needs wasigoc (goxxlang/wasigoc) as a sibling with src/runtime.hpp")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(W2G_S2_OS src/s2/os_wasi.cc)
  set(W2G_SAPI_REAL src/sapi/real_posix.cc)
elseif(WIN32)
  set(W2G_S2_OS src/s2/os_win.cc)
  set(W2G_SAPI_REAL src/sapi/real_win.cc src/sapi/tls_win.cc src/sapi/net_internal.cc
                    src/sapi/handles.cc)
else()
  set(W2G_S2_OS src/s2/os_posix.cc)
  set(W2G_SAPI_REAL src/sapi/real_posix.cc)
endif()

add_library(w2g
  src/pipe.cc
  src/bus.cc
  src/layer.cc
  src/stub.cc
  src/wasi.cc
  src/sapi/handle.cc
  src/sapi/channel.cc
  ${W2G_SAPI_REAL}
  src/gocvm_bridge.cc
  src/c/system.cc
  src/s2/result.cc
  src/s2/policy.cc
  src/s2/comms.cc
  src/s2/buffer.cc
  src/s2/executor.cc
  src/s2/sandbox.cc
  src/s2/client.cc
  src/abac.cc
  src/path.cc
  src/shim.cc
  ${W2G_S2_OS}
)

target_include_directories(w2g
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${W2G_WASIGO_DIR}/src
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(w2g PRIVATE /W4)
else()
  target_compile_options(w2g PRIVATE -Wall -Wextra -Wpedantic)
endif()

# System sandbox (host files / env / s2 spawn) is a compile-time opt-in.
# The library itself is built with it ON so in-tree s2 tests can spawn.
# Consumers of Shim must pass -DW2G_ABAC_SYSTEM=1 (and an ABAC policy)
# to actually reach fopen/getenv; without it Shim methods compile to deny.
option(W2G_ABAC_SYSTEM "Compile-time enable System sandbox (host OS / s2 spawn)" ON)
if(W2G_ABAC_SYSTEM)
  target_compile_definitions(w2g PRIVATE W2G_ABAC_SYSTEM=1)
endif()

find_package(Threads REQUIRED)
target_link_libraries(w2g PUBLIC Threads::Threads)

if(WIN32)
  target_link_libraries(w2g PUBLIC ws2_32 iphlpapi secur32 crypt32 netapi32)
endif()

enable_testing()

add_executable(w2g_s2_sandboxee examples/s2/sandboxee.cc)
target_link_libraries(w2g_s2_sandboxee PRIVATE w2g)

add_executable(w2g_tests
  tests/test_main.cc
  tests/pipe/ping.cc
  tests/bus/pubsub.cc
  tests/stub/layers.cc
  tests/sapi/handle.cc
  tests/layer/extra.cc
  tests/c/api.cc
  tests/s2/comms.cc
  tests/s2/sandbox.cc
  tests/s2/safety.cc
  tests/abac/shim.cc
)
target_include_directories(w2g_tests PRIVATE tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(w2g_tests PRIVATE w2g)
target_compile_definitions(w2g_tests PRIVATE W2G_ABAC_SYSTEM=1)
add_dependencies(w2g_tests w2g_s2_sandboxee)
add_test(NAME w2g_tests COMMAND w2g_tests)

add_executable(w2g_nosystem_test tests/abac/nosystem.cc)
target_include_directories(w2g_nosystem_test PRIVATE tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(w2g_nosystem_test PRIVATE w2g)
target_compile_definitions(w2g_nosystem_test PRIVATE W2G_ABAC_SYSTEM=0)
add_test(NAME w2g_nosystem_test COMMAND w2g_nosystem_test)

add_executable(w2g_bridge examples/bridge/main.cc)
target_link_libraries(w2g_bridge PRIVATE w2g)

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT TARGET w2g_wasm)
    add_executable(w2g_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/c/wasi_start.cc")
    target_link_libraries(w2g_wasm PRIVATE "-Wl,--whole-archive" w2g "-Wl,--no-whole-archive")
    target_link_options(w2g_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
    set_target_properties(w2g_wasm PROPERTIES OUTPUT_NAME "w2g")
  endif()
endif()
