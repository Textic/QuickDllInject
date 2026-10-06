# QuickDllInject Plugin CMakeLists.txt
cmake_minimum_required(VERSION 3.30.0 FATAL_ERROR)

project(QuickDllInject LANGUAGES C)

set(QUICKDLLINJECT_SOURCES
    src/main.c
    src/util.c
    src/menu.c
    src/options.c
    src/history.c
    src/engine.c
    src/inject.c
    src/stealth.c
    src/manualmap.c
    src/mapimage.c
    src/mapstub.c
    src/version.rc
    src/QuickDllInject.rc
)

set(QUICKDLLINJECT_HEADERS
    src/QuickDllInject.h
    src/resource.h
)

add_library(QuickDllInject SHARED
    ${QUICKDLLINJECT_SOURCES}
    ${QUICKDLLINJECT_HEADERS}
)

target_include_directories(QuickDllInject PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SI_ROOT}/SystemInformer/include
    ${SI_ROOT}/phlib/include
    ${SI_ROOT}/phnt/include
    ${SI_ROOT}/SystemInformer/sdk
)

target_link_libraries(QuickDllInject PRIVATE
    SystemInformer
    phlib
)
