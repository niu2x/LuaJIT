# Maybe useful flags
- -fomit-frame-pointer

# compile-time macro
- LJ_ABI_SOFTFP
- LJ_ABI_SHADOW_STACK

# compile
- build minilua
- generate buildvm_arch.h
    `host/minilua ../dynasm/dynasm.lua   -D ENDIAN_LE -D P64 -D JIT -D FFI -D FPU -D HFABI -D SHADOW_STACK -D VER= -o host/buildvm_arch.h vm_x64.dasc`
- 
