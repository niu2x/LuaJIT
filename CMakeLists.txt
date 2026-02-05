cmake_minimum_required(VERSION 3.17.0 FATAL_ERROR)

project(LuaJIT VERSION 0.1.0 LANGUAGES CXX C ASM)

add_executable(luajit "")

target_sources(luajit PRIVATE
    src/lib_aux.c
    src/lib_base.c
    src/lib_bit.c
    src/lib_buffer.c
    src/lib_debug.c
    src/lib_init.c
    src/lib_io.c
    src/lib_math.c
    src/lib_os.c
    src/lib_package.c
    src/lib_string.c
    src/lib_table.c
    src/lj_alloc.c
    src/lj_api.c
    src/lj_assert.c
    src/lj_bc.c
    src/lj_bcread.c
    src/lj_bcwrite.c
    src/lj_buf.c
    src/lj_char.c
    src/lj_debug.c
    src/lj_dispatch.c
    src/lj_err.c
    src/lj_func.c
    src/lj_gc.c
    src/lj_lex.c
    src/lj_lib.c
    src/lj_load.c
    src/lj_meta.c
    src/lj_obj.c
    src/lj_parse.c
    src/lj_prng.c
    src/lj_serialize.c
    src/lj_state.c
    src/lj_str.c
    src/lj_strfmt.c
    src/lj_strfmt_num.c
    src/lj_strscan.c
    src/lj_tab.c
    src/lj_udata.c
    src/lj_udata.c
    src/lj_vm.S
    src/luajit.c
    src/lj_vmmath.c
)

target_compile_definitions(luajit PRIVATE LUAJIT_UNWIND_EXTERNAL)

target_link_libraries(luajit PRIVATE m dl)