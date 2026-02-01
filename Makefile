build_linux:
	@echo "mkdir .gen/"
	@mkdir -p ./gen

	@echo "build minilua"
	@cmake -S minilua -B build/minilua -DCMAKE_BUILD_TYPE=Release
	@cmake --build build/minilua

	@echo "DYNASM host/buildvm_arch.h"
	@cd src && ../build/minilua/minilua ../dynasm/dynasm.lua -D ENDIAN_LE -D P64 -D JIT -D FFI -D FPU -D HFABI -D SHADOW_STACK -D VER= -o ../gen/buildvm_arch.h vm_x64.dasc

	@echo "build buildvm"
	@cmake -S buildvm -B build/buildvm -DCMAKE_BUILD_TYPE=Release -DLJ_TARGET=LUAJIT_ARCH_x64 -DLJ_ARCH_HASFPU=ON
	@cmake --build build/buildvm

