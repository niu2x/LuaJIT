default:
	$(MAKE) -C src

benchmark:
	src/luajit benchmark.lua

.PHONY: default