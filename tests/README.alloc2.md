# alloc2 byte accounting

The command remains `collectgarbage("diff")`. The first call establishes a baseline;
each later call compares and rolls the baseline, as before. The existing intrusive
BFS, address sorting and 16 GC passes are unchanged. No new GC header fields,
allocation/free hooks, reference maps or allocation stacks are added.

## Output

- `added_bytes`: self allocation bytes of newly seen addresses.
- `removed_bytes`: saved self bytes of addresses missing from the current walk.
- `resized_delta_bytes`: size changes at addresses present in both snapshots.
- `self_bytes`: leaf object bytes on an existing reference path.
- `[alloc2-resize]`: old/new size and current reference path of a resized object.
- `lua_bytes`: `g->gc.total`, the basis of `collectgarbage("count")`.
- `reachable_bytes`: sum of self sizes for this existing traversal's object set.
- `other_bytes`: `lua_bytes - reachable_bytes`. This includes VM buffers and
  allocations/objects not covered by this traversal; it is not automatically a leak.
- `string_hash_bytes` and `temp_buffer_bytes`: two known VM buffer capacities,
  already included in Lua total. Do not add them to the total again.
- `size_metadata_bytes`: actual extra libc buffer capacity used by the size ledger;
  excluded from Lua total. The existing alloc2 address/path buffers are unchanged.

The byte identities are:

```
added_bytes - removed_bytes + resized_delta_bytes = reachable_delta_bytes
reachable_delta_bytes + other_delta_bytes = lua_delta_bytes
```

Self sizes follow LuaJIT's freeing routines. Table bytes include array and hash
capacity, including the original colocated allocation after array separation.
Strings include their header and trailing zero. Userdata include the Lua header
and payload, not the native objects they point to. Allocator overhead, free arenas,
native node memory and process PSS are outside this accounting.

Missing from the traversal does not necessarily mean freed (e.g. ignore filters).
Paths are the original BFS-selected paths, not exclusive ownership. Shared objects
are counted once. As with the original pointer diff, address reuse can classify a
replacement as a survivor/resize; the aggregate byte identity still holds.

## Cost and checks

Only diff captures compute sizes: one sequential O(N) header/capacity scan after
the existing sort, then size comparisons inside the existing address merge. Resize output temporarily reuses each changed object's existing `debug_list`
links and `debug_counter` scratch (saved old size), after the BFS queue is drained
and before descendant counters are calculated. The list is drained and counters
reset before counting additions. No extra scan, resize buffer, hash map or sort
is needed to emit resize paths. Each snapshot stores one uint32 size per object;
two million-entry snapshots add about 7.63 MiB. Buffers retain their high-water
capacity for reuse. An unsupported/over-4-GiB self size or a size-buffer allocation
failure aborts that capture rather than silently truncating bytes.

Compile tests against the same source tree and static library:

```
cc -O2 -Isrc tests/alloc2_bytes.c src/libluajit.a -lm -o /tmp/alloc2_bytes
/tmp/alloc2_bytes
cc -O2 -Isrc tests/alloc2_bench.c src/libluajit.a -lm -o /tmp/alloc2_bench
/tmp/alloc2_bench 1000000
```

Use the same architecture/compiler for both old/new libraries. The benchmark
includes the original 16 GCs, traversal, sort and output to `/dev/null`; it times
baseline, unchanged captures, numeric table growth, and 10,000 new tables.
`alloc2_bytes.c` verifies string/userdata sizes, inline-to-separate table arrays,
hash growth, shrinkage, FFI, ignored snapshots and both accounting identities.
