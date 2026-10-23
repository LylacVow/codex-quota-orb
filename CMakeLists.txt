cmake_minimum_required(VERSION 3.20)

project(CodexQuotaOrb VERSION 0.2.0 LANGUAGES CXX RC)

add_executable(CodexQuotaOrb WIN32
    src/main.cpp
    src/resource.rc
)

target_compile_features(CodexQuotaOrb PRIVATE cxx_std_17)
target_compile_definitions(CodexQuotaOrb PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    set_property(TARGET CodexQuotaOrb PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(CodexQuotaOrb PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(CodexQuotaOrb PRIVATE /SUBSYSTEM:WINDOWS)
endif()

target_link_libraries(CodexQuotaOrb PRIVATE
    shell32
    advapi32
)

set_target_properties(CodexQuotaOrb PROPERTIES
    OUTPUT_NAME "CodexQuotaOrb"
)
