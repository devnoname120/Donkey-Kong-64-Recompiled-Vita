include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(RT64_FAST ON CACHE BOOL "Reduced RT64 renderer" FORCE)
set(RT64_FAST_VITAGL ON CACHE BOOL "vitaGL backend" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/lib/rt64 ${CMAKE_BINARY_DIR}/rt64)

option(DK64_VITA_RUNTIME "Build the ARMv7 N64 runtime" ON)
if(DK64_VITA_RUNTIME)
    set(RECOMP_ENABLE_MODS OFF CACHE BOOL "Dynamic code mods are unavailable on Vita" FORCE)
    set(RECOMP_TRACK_MEMORY_WRITES ON CACHE BOOL "Track framebuffer CPU writes" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime ${CMAKE_BINARY_DIR}/runtime)
    foreach(runtime_target ultramodern librecomp)
        target_include_directories(${runtime_target} PUBLIC
            ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include)
        target_compile_options(${runtime_target} PRIVATE -mcpu=cortex-a9 -mfpu=neon
            -mfloat-abi=hard -fno-strict-aliasing -ffunction-sections -fdata-sections)
    endforeach()
endif()

option(DK64_VITA_GAME "Build DK64 from the generated local ROM sources" ON)
option(DK64_VITA_DIAGNOSTICS "Enable Vita log files and diagnostic counters" ON)
if(DK64_VITA_GAME)
    if(NOT DK64_VITA_RUNTIME)
        message(FATAL_ERROR "DK64_VITA_GAME requires DK64_VITA_RUNTIME")
    endif()
    set(vita_recompiled_dir "${CMAKE_SOURCE_DIR}/build/RecompiledFuncsVita")
    if(NOT EXISTS "${vita_recompiled_dir}/recomp_overlays.inl"
        OR NOT EXISTS "${CMAKE_SOURCE_DIR}/rsp/n_aspMain.cpp")
        message(FATAL_ERROR "Generate Vita sources with ./N64Recomp us.vita.toml and ./RSPRecomp n_aspMain.toml first. For the graphics diagnostic alone, set DK64_VITA_GAME=OFF.")
    endif()
    file(STRINGS "${vita_recompiled_dir}/funcs.h" write_tracking_version
        REGEX "^#define RECOMP_GENERATED_WRITE_TRACKING 1$")
    if(NOT write_tracking_version)
        message(FATAL_ERROR "Rebuild N64Recomp from the pinned submodule and regenerate us.vita.toml: the current generated sources do not record memory writes.")
    endif()
    file(GLOB game_c CONFIGURE_DEPENDS "${vita_recompiled_dir}/*.c")
    file(GLOB game_cpp CONFIGURE_DEPENDS "${vita_recompiled_dir}/*.cpp")
    add_library(dk64_recompiled_vita STATIC ${game_c} ${game_cpp})
    target_include_directories(dk64_recompiled_vita PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include)
    target_compile_definitions(dk64_recompiled_vita PRIVATE RECOMP_TRACK_MEMORY_WRITES)
    target_compile_options(dk64_recompiled_vita PRIVATE -O2 -mcpu=cortex-a9 -mfpu=neon
        -mfloat-abi=hard -fno-strict-aliasing -ffunction-sections -fdata-sections
        $<$<COMPILE_LANGUAGE:C>:-Wno-implicit-function-declaration>)
    option(DK64_VITA_PROFILE_FUNCTIONS "Log samples of the active recompiled function for debugging stalls" OFF)
    if(DK64_VITA_PROFILE_FUNCTIONS)
        target_compile_definitions(ultramodern PRIVATE RECOMP_SCHEDULER_DIAGNOSTICS)
        target_compile_options(dk64_recompiled_vita PRIVATE -finstrument-functions)
    endif()
    add_executable(DK64Recompiled main.cpp renderer_context.cpp game_support.cpp gameplay_fixes.cpp profile.cpp timing.cpp framebuffer.cpp
        register_overlays.cpp
        ${CMAKE_SOURCE_DIR}/src/game/rom_decompression.cpp
        ${CMAKE_SOURCE_DIR}/rsp/n_aspMain.cpp)
    target_compile_features(DK64Recompiled PRIVATE cxx_std_20)
    option(DK64_VITA_TRACE_RENDERER "Capture startup and intro draws without function instrumentation" OFF)
    option(DK64_VITA_SCRIPTED_INPUT "Run the Adventure integration probe with separate saves" OFF)
    option(DK64_VITA_SCRIPTED_PAUSE "Exercise pause and resume during the Adventure probe" OFF)
    if(NOT DK64_VITA_DIAGNOSTICS AND (DK64_VITA_PROFILE_FUNCTIONS OR DK64_VITA_TRACE_RENDERER
        OR DK64_VITA_SCRIPTED_INPUT OR DK64_VITA_SCRIPTED_PAUSE OR RT64_FAST_VALIDATE_UPLOADS))
        message(FATAL_ERROR "Diagnostic instrumentation and scripted inputs require DK64_VITA_DIAGNOSTICS=ON")
    endif()
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS DK64_VITA_SCRIPTED_INPUT=$<BOOL:${DK64_VITA_SCRIPTED_INPUT}>)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS DK64_VITA_SCRIPTED_PAUSE=$<BOOL:${DK64_VITA_SCRIPTED_PAUSE}>)
    set_property(SOURCE renderer_context.cpp APPEND PROPERTY COMPILE_DEFINITIONS DK64_VITA_TRACE_RENDERER=$<BOOL:${DK64_VITA_TRACE_RENDERER}>)
    target_compile_definitions(DK64Recompiled PRIVATE DK64_VITA_PROFILE_FUNCTIONS=$<BOOL:${DK64_VITA_PROFILE_FUNCTIONS}>)
    target_compile_definitions(DK64Recompiled PRIVATE DK64_VITA_DIAGNOSTICS=$<BOOL:${DK64_VITA_DIAGNOSTICS}>)
    target_compile_options(DK64Recompiled PRIVATE -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard
        -fno-strict-aliasing -ffunction-sections -fdata-sections)
    target_include_directories(DK64Recompiled PRIVATE ${CMAKE_SOURCE_DIR}/include ${vita_recompiled_dir}
        ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/sse2neon)
    target_link_libraries(DK64Recompiled PRIVATE dk64_recompiled_vita librecomp ultramodern rt64 SDL2
        "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive"
        SceAudio_stub SceAudioIn_stub SceCtrl_stub SceTouch_stub SceHid_stub SceMotion_stub
        SceIme_stub SceAppUtil_stub ScePower_stub SceAppMgr_stub SceSysmodule_stub m)
    target_link_options(DK64Recompiled PRIVATE -Wl,--gc-sections -Wl,--wrap=osPiRawStartDma_recomp)
    if(NOT DK64_VITA_DIAGNOSTICS)
        target_link_options(DK64Recompiled PRIVATE -Wl,--strip-debug)
    endif()
    if(DK64_VITA_PROFILE_FUNCTIONS)
        target_link_options(DK64Recompiled PRIVATE -Wl,--wrap=osSetTimer_recomp
            -Wl,--wrap=osSetEventMesg_recomp -Wl,--wrap=osRecvMesg_recomp
            -Wl,--wrap=osSendMesg_recomp -Wl,--wrap=osSpTaskStartGo_recomp)
    endif()
    vita_create_self(DK64Recompiled.self DK64Recompiled UNSAFE)
    if(DK64_VITA_SCRIPTED_INPUT)
        vita_create_vpk(DK64AdventureProbe.vpk DK64RT001 DK64Recompiled.self VERSION 01.00 NAME "DK64 Adventure Probe")
    else()
        vita_create_vpk(DK64Recompiled.vpk DK64RE001 DK64Recompiled.self VERSION 01.00 NAME "Donkey Kong 64 Recompiled")
    endif()
endif()

# This is deliberately a separate application from the game: it verifies the
# graphics stack without claiming that the DK64 runtime has been ported.
add_executable(rt64_fast_smoke smoke.cpp)
if(DK64_VITA_RUNTIME)
    target_link_libraries(rt64_fast_smoke PRIVATE librecomp)
endif()
target_compile_options(rt64_fast_smoke PRIVATE -fno-strict-aliasing)
target_link_libraries(rt64_fast_smoke PRIVATE rt64
    "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive"
    SceCtrl_stub ScePower_stub SceTouch_stub SceAppMgr_stub m)
vita_create_self(rt64_fast_smoke.self rt64_fast_smoke UNSAFE)
vita_create_vpk(rt64_fast_smoke.vpk RT64F3001 rt64_fast_smoke.self
    VERSION 01.00 NAME "RT64 Fast Renderer Test")

add_executable(rt64_semaphore_smoke semaphore_smoke.cpp)
target_compile_features(rt64_semaphore_smoke PRIVATE cxx_std_17)
target_include_directories(rt64_semaphore_smoke PRIVATE ${CMAKE_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/concurrentqueue)
target_link_libraries(rt64_semaphore_smoke PRIVATE "-Wl,--whole-archive" pthread "-Wl,--no-whole-archive")
vita_create_self(rt64_semaphore_smoke.self rt64_semaphore_smoke UNSAFE)
vita_create_vpk(rt64_semaphore_smoke.vpk RT64S0001 rt64_semaphore_smoke.self
    VERSION 01.00 NAME "RT64 Semaphore Test")
