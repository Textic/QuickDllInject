# QuickDllInject Plugin CMakeLists.txt
cmake_minimum_required(VERSION 3.30.0 FATAL_ERROR)

project(QuickDllInject LANGUAGES C)

set(QUICKDLLINJECT_SOURCES
    main.c
    version.rc
)

set(QUICKDLLINJECT_HEADERS
    QuickDllInject.h
    resource.h
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
