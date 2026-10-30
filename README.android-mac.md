# 在 macOS 构建 Android LuaJIT

当前本地调试使用 `debug/alloc2`（跟踪 `xxl/debug/alloc2`）源码。本机默认 NDK 为 `/opt/android-sdk/ndk/21.4.7075529`，可通过 `ANDROID_NDK_HOME` 覆盖。

```bash
cd /Users/happy/project/LuaJIT
./build_android.sh all
```

分别构建：

```bash
./build_android.sh arm64-v8a
./build_android.sh armeabi-v7a
```

产物位于 `build/android/<ABI>/libluajit.a`，头文件在对应的 `include/` 中。默认不修改 animal-core。

构建并复制两个 ABI 的库与头文件到本地引擎：

```bash
./build_android.sh all --install /Users/happy/project/animal-git/animal-core
```

64 位默认 Android API 21，32 位默认 API 16，可用 `ANDROID_API64`、`ANDROID_API32` 覆盖。

## 为什么 32 位需要 Docker

这版 LuaJIT 的 `src/host/buildvm.c` 检查生成工具的指针宽度，生成 ARM32 代码需要 32 位的 buildvm。现代 macOS 无法运行 32 位进程，不能直接把旧命令中的 `gcc -m32` 换为 `clang`。

脚本使用 Docker 的 Debian 镜像构建 32 位 buildvm，通过 qemu-i386 执行；minilua 与 Android NDK 编译在 Mac 上运行。不会修改 LuaJIT 源码来绕过检查。32 位和 all 模式需要 Docker Desktop 已启动，首次构建会下载生成工具依赖；64 位模式不需要 Docker。

源码复制到临时目录进行构建，包含本地尚未提交的修改，两个 ABI 分别构建，退出时清理临时目录与容器。需要替换引擎库时显式传入 `--install`，然后重新构建 Android 游戏。

## alloc2 交互适配

本地 Android Intent 入口与 adb 客户端位于 animal-core / animal-build-tools。
本分支提供 `luaL_gcdebugoutput(FILE*)`，将 diff/findf/ignore 的输出切换到每次命令的私有结果文件；传入 NULL 恢复 stdout。

适配还修复了真实大堆诊断触发的递归排序栈溢出（改为非递归堆排序），以及长 chunk 名称使分配标签的 4 KiB 缓冲越界（改为 snprintf）。
`findf` 会检查对象类型，并正确返回零个 Lua 值；字节码 writer 保持公共四参数回调签名。
`diff` 增加基线、当前及新增对象数量摘要，计数含义仍是对象数，不是字节数。

## 与正常分支合并

`debug/alloc2` 已合并 `xxl/animal-v2.1.x`（`f7024628`），基于该分支的
LuaJIT 2.1.0-beta3，同时保留 alloc2 的分配追踪、dump/diff/findf/ignore
和输出重定向。完整 64 位 lightuserdata 通过上游的高位分段映射实现，
不再依赖旧 alloc2 的低 47 位地址限制。为项目旧 LuaSocket 等调用保留
`luaL_reg` 和旧引用宏的兼容头文件接口。
alloc2 扩展头在 32 位增加对齐填充，GCtab / GCcdata 使用静态断言保证
8 字节对齐，避免内嵌数组和 FFI 变长对象的对齐/容量假设被破坏。

本机原生回归程序 `tests/alloc2_smoke.c` 应链接本次源码构建的静态库：

```sh
clang -arch x86_64 -I<build>/src tests/alloc2_smoke.c <build>/src/libluajit.a \
  -lm -o /tmp/alloc2-smoke
/tmp/alloc2-smoke /tmp/alloc2-smoke.dump
```

测试覆盖高位 lightuserdata 的 C API、表键和闭包往返，以及 alloc2 命令、
长 loadstring、字节码、FFI、协程、JIT 和 5 万对象 diff。
