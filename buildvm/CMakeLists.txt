cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

project(LuaJIT-buildvm VERSION 0.1.0 LANGUAGES CXX C)

add_executable(buildvm "")

target_sources(buildvm PRIVATE
    buildvm.c
    buildvm_asm.c
    buildvm_fold.c
    buildvm_lib.c
    buildvm_peobj.c
)

target_include_directories(buildvm PRIVATE
    ../src/
    ../gen/
)

option(LJ_ARCH_HASFPU "help string describing LJ_ARCH_HASFPU" ON)
option(LJ_TARGET "help string describing LJ_TARGET" "")

if(${LJ_ARCH_HASFPU})
    target_compile_definitions(buildvm PRIVATE
        LJ_ARCH_HASFPU=1
    )
else()
    target_compile_definitions(buildvm PRIVATE
        LJ_ARCH_HASFPU=0
    )
endif()

target_compile_definitions(buildvm PRIVATE
    LUAJIT_TARGET=${LJ_TARGET}
)