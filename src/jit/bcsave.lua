----------------------------------------------------------------------------
-- LuaJIT module to save/list bytecode.
--
-- Copyright (C) 2005-2017 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
--
-- This module saves or lists the bytecode for an input file.
-- It's run by the -b command line option.
--
------------------------------------------------------------------------------
local jit = require("jit")
assert(jit.version_num == 20100, "LuaJIT core/library version mismatch")
local bit = require("bit")

-- Symbol name prefix for LuaJIT bytecode.
local LJBC_PREFIX = "luaJIT_BC_"

------------------------------------------------------------------------------

local function usage()
    io.stderr:write [[
Save LuaJIT bytecode: luajit -b[options] input output
  -l        Only list bytecode.
  -s        Strip debug info (default).
  -g        Keep debug info.
  -n name   Set module name (default: auto-detect from input name).
  -t type   Set output file type (default: auto-detect from output name).
  -a arch   Override architecture for object files (default: native).
  -o os     Override OS for object files (default: native).
  -e chunk  Use chunk string as input.
  --        Stop handling options.
  -         Use stdin as input and/or stdout as output.

File types: c h obj o raw (default)
]]
    os.exit(1)
end

local function check(ok, ...)
    if ok then
        return ok, ...
    end
    io.stderr:write("luajit: ", ...)
    io.stderr:write("\n")
    os.exit(1)
end

local function readfile(input)
    if type(input) == "function" then
        return input
    end
    if input == "-" then
        input = nil
    end
    return check(loadfile(input))
end

local function savefile(name, mode)
    if name == "-" then
        return io.stdout
    end
    return check(io.open(name, mode))
end

------------------------------------------------------------------------------

local map_type = { raw = "raw", c = "c", h = "h", o = "obj", obj = "obj" }

local map_arch = {
    x86 = true,
    x64 = true,
    arm = true,
    arm64 = true,
    arm64be = true,
    ppc = true,
    mips = true,
    mipsel = true,
}

local map_os = {
    linux = true,
    windows = true,
    osx = true,
    freebsd = true,
    netbsd = true,
    openbsd = true,
    dragonfly = true,
    solaris = true,
}

local function checkarg(str, map, err)
    str = string.lower(str)
    local s = check(map[str], "unknown ", err)
    return s == true and str or s
end

local function detecttype(str)
    local ext = string.match(string.lower(str), "%.(%a+)$")
    return map_type[ext] or "raw"
end

local function checkmodname(str)
    check(string.match(str, "^[%w_.%-]+$"), "bad module name")
    return string.gsub(str, "[%.%-]", "_")
end

local function detectmodname(str)
    if type(str) == "string" then
        local tail = string.match(str, "[^/\\]+$")
        if tail then
            str = tail
        end
        local head = string.match(str, "^(.*)%.[^.]*$")
        if head then
            str = head
        end
        str = string.match(str, "^[%w_.%-]+")
    else
        str = nil
    end
    check(str, "cannot derive module name, use -n name")
    return string.gsub(str, "[%.%-]", "_")
end

------------------------------------------------------------------------------

local function bcsave_tail(fp, output, s)
    local ok, err = fp:write(s)
    if ok and output ~= "-" then
        ok, err = fp:close()
    end
    check(ok, "cannot write ", output, ": ", err)
end

local function bcsave_raw(output, s)
    local fp = savefile(output, "wb")
    bcsave_tail(fp, output, s)
end

local function bclist(input, output)
    local f = readfile(input)
    require("jit.bc").dump(f, savefile(output, "w"), true)
end

local function bcsave(ctx, input, output)
    local f = readfile(input)
    local s = string.dump(f, ctx.strip)
    local t = ctx.type
    if not t then
        t = detecttype(output)
        ctx.type = t
    end
    bcsave_raw(output, s)
end

local function docmd(...)
    local arg = { ... }
    local n = 1
    local list = false
    local ctx = {
        strip = true,
        arch = jit.arch,
        os = string.lower(jit.os),
        type = false,
        modname = false,
    }
    while n <= #arg do
        local a = arg[n]
        if type(a) == "string" and string.sub(a, 1, 1) == "-" and a ~= "-" then
            table.remove(arg, n)
            if a == "--" then
                break
            end
            for m = 2, #a do
                local opt = string.sub(a, m, m)
                if opt == "l" then
                    list = true
                elseif opt == "s" then
                    ctx.strip = true
                elseif opt == "g" then
                    ctx.strip = false
                else
                    if arg[n] == nil or m ~= #a then
                        usage()
                    end
                    if opt == "e" then
                        if n ~= 1 then
                            usage()
                        end
                        arg[1] = check(loadstring(arg[1]))
                    elseif opt == "n" then
                        ctx.modname = checkmodname(table.remove(arg, n))
                    elseif opt == "t" then
                        ctx.type = checkarg(
                            table.remove(arg, n), map_type, "file type"
                        )
                    elseif opt == "a" then
                        ctx.arch = checkarg(
                            table.remove(arg, n), map_arch, "architecture"
                        )
                    elseif opt == "o" then
                        ctx.os = checkarg(
                            table.remove(arg, n), map_os, "OS name"
                        )
                    else
                        usage()
                    end
                end
            end
        else
            n = n + 1
        end
    end
    if list then
        if #arg == 0 or #arg > 2 then
            usage()
        end
        bclist(arg[1], arg[2] or "-")
    else
        if #arg ~= 2 then
            usage()
        end
        bcsave(ctx, arg[1], arg[2])
    end
end

------------------------------------------------------------------------------

-- Public module functions.
return {
    start = docmd, -- Process -b command line option.
}

