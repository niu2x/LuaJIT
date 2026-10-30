# Lua allocator Used / Reserved display

`PlogStatusNode` displays `L U199.9 R399.3MB` on Android.

- `U`: existing sum of `gc.total` for the independent Lua states managed by UBF.
  These are requested bytes not yet freed, including unreachable objects awaiting
  collection, table/stack capacities and VM buffers. This is not a reachability count.
- `R`: `luaJIT_getreservedbytes()`, process-wide system mappings still held by this
  copy of the bundled LuaJIT allocator, including ordinary segments and direct-mapped
  large allocations. Free blocks, allocator headers and alignment remain included.
  It is mapped address space, not RSS/PSS or a measurement of physically resident pages.

JIT machine-code mappings, alloc2's libc-allocated metadata, and native objects
referenced by Lua are not automatically part of `R`. If built with
`LUAJIT_USE_SYSMALLOC`, this mapping counter is unavailable (zero). Non-Android
Plog displays retain the previous format when the counter is unavailable.

The counter changes only after successful system mapping, resizing or unmapping.
Updates and reads are atomic across independent allocator instances. It does not
walk objects, scan `/proc`, allocate buffers or trigger GC when the display reads it.

## Android verification

Build and install the new static libraries and headers locally:

```sh
./build_android.sh all --install /path/to/animal-core
```

For each ABI, compile `tests/alloc_reserved.c` against its matching static library:

```sh
$CC -O2 -Isrc tests/alloc_reserved.c build/android/$ABI/libluajit.a -lm -o /tmp/alloc-reserved
```

This exercises two states, a direct-mapped allocation, full GC and closing both
states. Reserved must return to zero after both states are closed.

The Linux/Android syscall test includes the allocator source and intercepts the
system calls at link time. Compile it without also linking `libluajit.a`:

```sh
$CC -O2 -pthread -Wno-gnu-null-pointer-arithmetic tests/alloc_reserved_syscalls.c \
  -Wl,--wrap=mmap,--wrap=mremap,--wrap=munmap -o /tmp/alloc-reserved-syscalls
```

It checks failed map/remap/unmap operations, successful growth/shrinkage, and four
concurrent independent allocators. Run the cross-compiled executables on the device.
