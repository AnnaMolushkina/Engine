cmake_minimum_required(VERSION 3.21)

# Все библиотеки ставит vcpkg в manifest-режиме (vcpkg.json) при первой конфигурации CMake:
# Assimp, glm, stb, nlohmann-json, enkiTS, Tracy. Никаких ручных путей в настройках проекта.
project(Engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(assimp CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(enkiTS CONFIG REQUIRED)
   include(FetchContent)
   set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
   set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
   if(EXISTS "C:/dvizki/tracy-src/public/TracyClient.cpp")
       FetchContent_Declare(tracy SOURCE_DIR "C:/dvizki/tracy-src")
   else()
       FetchContent_Declare(tracy
           URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.zip)
   endif()
   FetchContent_MakeAvailable(tracy)

set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Common")
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Chapter 4 Direct3D Initialization/Init Direct3D")

# Исходники движка: все .cpp/.h/.hlsl из папки проекта (новые файлы подхватываются
# при повторной конфигурации CMake)
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${ENGINE_DIR}/*.cpp"
    "${ENGINE_DIR}/*.h"
)
file(GLOB ENGINE_SHADERS CONFIGURE_DEPENDS "${ENGINE_DIR}/Shaders/*.hlsl")
set_source_files_properties(${ENGINE_SHADERS} PROPERTIES HEADER_FILE_ONLY TRUE) # компилируются в рантайме

set(COMMON_SOURCES
    "${COMMON_DIR}/d3dApp.cpp"
    "${COMMON_DIR}/d3dApp.h"
    "${COMMON_DIR}/d3dUtil.cpp"
    "${COMMON_DIR}/d3dUtil.h"
    "${COMMON_DIR}/d3dx12.h"
    "${COMMON_DIR}/GameTimer.cpp"
    "${COMMON_DIR}/GameTimer.h"
    "${COMMON_DIR}/MathHelper.cpp"
    "${COMMON_DIR}/MathHelper.h"
    "${COMMON_DIR}/UploadBuffer.h"
)

add_executable(Engine WIN32 ${COMMON_SOURCES} ${ENGINE_SOURCES} ${ENGINE_SHADERS})

target_include_directories(Engine PRIVATE "${COMMON_DIR}" "${ENGINE_DIR}" ${Stb_INCLUDE_DIR})
target_compile_definitions(Engine PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_definitions(Engine PRIVATE TRACY_ENABLE)
target_link_libraries(Engine PRIVATE
    assimp::assimp
    glm::glm
    nlohmann_json::nlohmann_json
    enkiTS::enkiTS
    Tracy::TracyClient
    d3d12 dxgi d3dcompiler shell32
)

if(MSVC)
    # /utf-8: исходники в UTF-8 (кириллица в комментариях и строках)
    target_compile_options(Engine PRIVATE /W3 /utf-8)
    target_compile_definitions(Engine PRIVATE _CRT_SECURE_NO_WARNINGS)
    # .pdb и для Release: стек вызовов в engine.log при падении и в Tracy
    target_compile_options(Engine PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(Engine PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

# Рядом с exe: конфиг, сцена, шейдеры и ассеты (движок при старте переходит в папку exe)
add_custom_command(TARGET Engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ENGINE_DIR}/config.json" "$<TARGET_FILE_DIR:Engine>/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ENGINE_DIR}/scene.json" "$<TARGET_FILE_DIR:Engine>/scene.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ENGINE_DIR}/Shaders" "$<TARGET_FILE_DIR:Engine>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${ENGINE_DIR}/assets" "$<TARGET_FILE_DIR:Engine>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/tools/bench.ps1" "$<TARGET_FILE_DIR:Engine>/bench.ps1"
)
set_property(TARGET Engine PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:Engine>")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Engine)
