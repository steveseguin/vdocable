cmake_minimum_required(VERSION 3.24)
project(vdocable VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(VDO_CABLE_BUILD_TESTS "Build VDO Cable tests" ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_COROUTINE_ABI=1)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(VDO_CABLE_GAME_CAPTURE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../game-capture/native-qt" CACHE PATH "Path to game-capture/native-qt")

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

include(FetchContent)

FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.5.0
)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
add_compile_definitions(MBEDTLS_SSL_DTLS_SRTP)
FetchContent_MakeAvailable(mbedtls)

set(MbedTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MbedTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
set(MbedX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
set(MbedCrypto_LIBRARY mbedcrypto CACHE STRING "" FORCE)
set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto CACHE STRING "" FORCE)
set(MbedTLS_FOUND TRUE CACHE BOOL "" FORCE)
set(MBEDTLS_FOUND TRUE CACHE BOOL "" FORCE)

if(NOT TARGET MbedTLS::mbedtls)
    add_library(MbedTLS::mbedtls ALIAS mbedtls)
endif()
if(NOT TARGET MbedTLS::mbedx509)
    add_library(MbedTLS::mbedx509 ALIAS mbedx509)
endif()
if(NOT TARGET MbedTLS::mbedcrypto)
    add_library(MbedTLS::mbedcrypto ALIAS mbedcrypto)
endif()

FetchContent_Declare(
    libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG v0.24.0
)
set(NO_MEDIA OFF CACHE BOOL "" FORCE)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
set(USE_SYSTEM_SRTP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALL OFF CACHE BOOL "" FORCE)
set(LIBSRTP_INSTALL_CMAKE OFF CACHE BOOL "" FORCE)
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
set(NO_TESTS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libdatachannel)

if(TARGET datachannel-static)
    target_link_directories(datachannel-static PUBLIC ${CMAKE_BINARY_DIR}/lib)
endif()
if(TARGET juice-static)
    target_link_directories(juice-static PUBLIC ${CMAKE_BINARY_DIR}/lib)
endif()

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG v1.4
)
set(OPUS_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opus)

set(GAME_CAPTURE_ROOT ${VDO_CABLE_GAME_CAPTURE_ROOT})
if(NOT EXISTS "${GAME_CAPTURE_ROOT}/include/versus/signaling/vdo_signaling.h")
    message(FATAL_ERROR "Missing Game Capture dependency at ${GAME_CAPTURE_ROOT}. Clone steveseguin/game-capture beside this repo or set -DVDO_CABLE_GAME_CAPTURE_ROOT=<path>.")
endif()

set(ROUTER_SOURCES
    src/main.cpp
    src/app/audio_route_publisher.cpp
    src/ui/main_window.cpp
    src/ui/route_widget.cpp
    src/util/router_text.cpp
    src/webrtc/audio_webrtc_client.cpp
    ${GAME_CAPTURE_ROOT}/src/signaling/vdo_signaling.cpp
    ${GAME_CAPTURE_ROOT}/src/audio/opus_encoder.cpp
    ${GAME_CAPTURE_ROOT}/src/audio/window_audio_capture_core.cpp
)

set(ROUTER_HEADERS
    include/router/app/audio_route_publisher.h
    include/router/ui/main_window.h
    include/router/ui/route_widget.h
    include/router/util/router_text.h
    include/router/webrtc/audio_webrtc_client.h
)

if(WIN32)
    list(APPEND ROUTER_SOURCES resources/app.rc)
endif()

add_executable(vdocable WIN32 ${ROUTER_SOURCES} ${ROUTER_HEADERS})
set_target_properties(vdocable PROPERTIES
    AUTOMOC ON
    OUTPUT_NAME "vdocable"
)

target_compile_definitions(vdocable PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_include_directories(vdocable PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GAME_CAPTURE_ROOT}/include
    ${opus_SOURCE_DIR}/include
    ${libdatachannel_SOURCE_DIR}/deps/json/include
)

target_link_libraries(vdocable PRIVATE
    Threads::Threads
    Qt6::Widgets
    spdlog::spdlog
    datachannel-static
    mbedtls
    mbedx509
    mbedcrypto
    opus
)

if(WIN32)
    target_link_libraries(vdocable PRIVATE
        ole32
        oleaut32
        uuid
        user32
        gdi32
        shlwapi
        winmm
        runtimeobject
    )
endif()

if(VDO_CABLE_BUILD_TESTS)
    enable_testing()
    add_executable(vdocable_logic_test
        tests/router_text_test.cpp
        src/util/router_text.cpp
    )
    target_include_directories(vdocable_logic_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(vdocable_logic_test PRIVATE Threads::Threads)
    target_link_libraries(vdocable_logic_test PRIVATE mbedcrypto)
    add_test(NAME vdocable_logic_test COMMAND vdocable_logic_test)
endif()
