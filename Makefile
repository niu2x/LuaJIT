default:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build -j

benchmark:
	./build/luajit benchmark.lua

.PHONY: default