# 在 macOS 构建 Android LuaJIT

使用 `animal-v2.1.x` 源码。本机默认 NDK 为 `/opt/android-sdk/ndk/21.4.7075529`，可通过 `ANDROID_NDK_HOME` 覆盖。

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
