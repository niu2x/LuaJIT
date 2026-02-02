#!/usr/bin/env lua
-- Lua解释器性能测试套件
-- 用法: lua benchmark.lua [测试名称] [迭代次数]

local args = {...}
local test_name = args[1] or "all"
local iterations = tonumber(args[2]) or 1000000

-- 计时器函数
local function time_function(name, func, ...)
    local start_time = os.clock()
    local result = func(...)
    local end_time = os.clock()
    local elapsed = end_time - start_time
    print(string.format("%-30s: %.6f 秒", name, elapsed))
    return result, elapsed
end

-- 1. 基础算术运算测试
local function test_arithmetic(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i * 1.5 - 0.5 / 2.0
    end
    return sum
end

-- 2. 整数运算测试
local function test_integer_arithmetic(n)
    local sum = 0
    for i = 1, n do
        sum = sum + i * 3 - 2
    end
    return sum
end

-- 3. 函数调用开销测试
local function add(a, b) return a + b end
local function test_function_calls(n)
    local total = 0
    for i = 1, n do
        total = add(total, i)
    end
    return total
end

-- 4. 递归函数测试（斐波那契）
local function fibonacci(n)
    if n <= 1 then return n end
    return fibonacci(n-1) + fibonacci(n-2)
end

local function test_recursion()
    return fibonacci(30)  -- 警告：这个会较慢
end

-- 5. 表操作测试
local function test_table_operations(n)
    local t = {}
    -- 插入
    for i = 1, n do
        t[i] = i
    end
    -- 访问
    local sum = 0
    for i = 1, n do
        sum = sum + t[i]
    end
    -- 删除
    for i = 1, n do
        t[i] = nil
    end
    return sum
end

-- 6. 字符串连接测试
local function test_string_concat(n)
    local s = ""
    for i = 1, n do
        s = s .. "a"
    end
    return #s
end

-- 7. 字符串处理测试
local function test_string_operations(n)
    local s = "Hello, World! " .. os.date()
    local total = 0
    for i = 1, n do
        total = total + #s
        s = s:sub(1, -2)  -- 移除最后一个字符
        s = s .. "x"      -- 添加一个字符
    end
    return total
end

-- 8. 数组求和（数值for循环）
local function test_array_sum(n)
    local arr = {}
    for i = 1, n do
        arr[i] = i
    end
    
    local sum = 0
    for i = 1, #arr do
        sum = sum + arr[i]
    end
    return sum
end

-- 9. 泛型for循环测试
local function test_generic_for(n)
    local t = {}
    for i = 1, n do
        t[i] = i
    end
    
    local sum = 0
    for _, v in ipairs(t) do
        sum = sum + v
    end
    return sum
end

-- 10. 闭包测试
local function test_closures(n)
    local function make_adder(x)
        return function(y) return x + y end
    end
    
    local add5 = make_adder(5)
    local total = 0
    for i = 1, n do
        total = add5(i)
    end
    return total
end

-- 11. 协程测试
local function test_coroutines(n)
    local co = coroutine.create(function()
        for i = 1, n do
            coroutine.yield(i)
        end
    end)
    
    local sum = 0
    for i = 1, n do
        local _, value = coroutine.resume(co)
        if value then
            sum = sum + value
        end
    end
    return sum
end

-- 12. 元表/metatable测试
local function test_metatables(n)
    local mt = {
        __add = function(a, b)
            local sum = 0
            if type(a) == 'number' then
                sum = sum + a
            else
                sum = sum + a.value
            end

            if type(b) == 'number' then
                sum = sum + b
            else
                sum = sum + b.value
            end
            return sum
        end
    }
    
    local total = 0
    for i = 1, n do
        local a = {value = i}
        local b = {value = i * 2}
        setmetatable(a, mt)
        setmetatable(b, mt)
        total = total + a + b
    end
    return total
end

-- 13. 内存分配测试
local function test_memory_allocation(n)
    local total = 0
    for i = 1, n do
        local t = {a = i, b = i*2, c = i*3}
        total = total + t.a + t.b + t.c
    end
    return total
end

-- 14. 素数筛选（算法测试）
local function test_sieve(n)
    local is_prime = {}
    for i = 2, n do
        is_prime[i] = true
    end
    
    for i = 2, math.sqrt(n) do
        if is_prime[i] then
            for j = i*i, n, i do
                is_prime[j] = false
            end
        end
    end
    
    local count = 0
    for i = 2, n do
        if is_prime[i] then
            count = count + 1
        end
    end
    return count
end

-- 15. 尾递归优化测试
local function tail_recursive_factorial(n, acc)
    acc = acc or 1
    if n <= 1 then return acc end
    return tail_recursive_factorial(n-1, n * acc)
end

local function test_tail_recursion()
    return tail_recursive_factorial(1000)
end

-- 16. 钩子函数测试 (sethook)
local function test_sethook(n)
    local count = 0
    local hook_func = function()
        count = count + 1
    end
    
    -- 设置钩子
    debug.sethook(hook_func, "c")
    
    -- 执行一些操作触发钩子
    for i = 1, n do
        local x = i * 2
    end
    
    -- 关闭钩子
    debug.sethook()
    
    return count
end

-- 17. 元表操作测试 (setmetatable/getmetatable)
local function test_setmetatable(n)
    local mt = {}
    local count = 0
    
    for i = 1, n do
        local t = {}
        setmetatable(t, mt)
        local get_mt = getmetatable(t)
        if get_mt == mt then
            count = count + 1
        end
    end
    
    return count
end

-- 18. 函数环境测试 (setfuncenv/getfuncenv)
local function test_setfuncenv(n)
    local env = {print = print}
    local func = function(x) return x * 2 end
    local count = 0
    
    for i = 1, n do
        setfenv(func, env)
        local get_env = getfenv(func)
        if get_env == env then
            count = count + 1
        end
    end
    
    return count
end

-- 19. 原始表操作测试 (rawget/rawset)
local function test_raw_operations(n)
    local t = {}
    local mt = {
        __index = function() return 0 end,
        __newindex = function() end
    }
    setmetatable(t, mt)
    
    local sum = 0
    for i = 1, n do
        rawset(t, i, i)
        sum = sum + rawget(t, i)
    end
    
    return sum
end

-- 20. 字符串加载测试 (loadstring)
local function test_loadstring(n)
    local count = 0
    local code = "return x * 2"
    
    for i = 1, n do
        local func = loadstring(code)
        if func then
            setfenv(func, {x = i})
            local result = func()
            if result == i * 2 then
                count = count + 1
            end
        end
    end
    
    return count
end

-- 21. 错误处理测试 (pcall/xpcall)
local function test_pcall(n)
    local func = function(x)
        if x % 1000 == 0 then
            error("test error")
        end
        return x * 2
    end
    
    local count = 0
    for i = 1, n do
        local success, result = pcall(func, i)
        if success then
            count = count + 1
        end
    end
    
    return count
end

-- 22. 类型判断测试 (type)
local function test_type(n)
    local values = {1, "string", true, nil, {}, function() end}
    local count = 0
    
    for i = 1, n do
        local v = values[(i % #values) + 1]
        local t = type(v)
        if t then
            count = count + 1
        end
    end
    
    return count
end

-- 测试套件
local tests = {
    ["arithmetic"] = {test_arithmetic, iterations},
    ["integer"] = {test_integer_arithmetic, iterations},
    ["function"] = {test_function_calls, iterations},
    ["recursion"] = {test_recursion},  -- 注意：这个较慢
    ["table"] = {test_table_operations, math.min(iterations, 100000)},
    ["string"] = {test_string_concat, math.min(iterations, 10000)},
    ["stringops"] = {test_string_operations, math.min(iterations, 10000)},
    ["array"] = {test_array_sum, math.min(iterations, 100000)},
    ["forloop"] = {test_generic_for, math.min(iterations, 100000)},
    ["closure"] = {test_closures, math.min(iterations, 100000)},
    ["coroutine"] = {test_coroutines, math.min(iterations, 10000)},
    ["metatable"] = {test_metatables, math.min(iterations, 10000)},
    ["memory"] = {test_memory_allocation, math.min(iterations, 10000)},
    ["sieve"] = {test_sieve, 10000},  -- 固定大小
    ["tailrec"] = {test_tail_recursion},
    ["sethook"] = {test_sethook, math.min(iterations, 10000)},
    ["setmetatable"] = {test_setmetatable, math.min(iterations, 100000)},
    ["setfuncenv"] = {test_setfuncenv, math.min(iterations, 100000)},
    ["rawops"] = {test_raw_operations, math.min(iterations, 100000)},
    ["loadstring"] = {test_loadstring, math.min(iterations, 1000)},
    ["pcall"] = {test_pcall, math.min(iterations, 100000)},
    ["type"] = {test_type, math.min(iterations, 100000)},
}

-- 运行指定测试
local function run_test(name, test_info)
    local func = test_info[1]
    local param = test_info[2] or iterations
    
    print(string.rep("=", 60))
    print(string.format("测试: %s (n = %s)", name, param))
    print(string.rep("-", 60))
    
    local result, elapsed = time_function(name, func, param)
    
    if result then
        print(string.format("结果: %s", tostring(result)))
    end
    
    -- 计算每秒操作数
    if param and elapsed > 0 then
        local ops_per_second = param / elapsed
        print(string.format("性能: %.2f 次操作/秒", ops_per_second))
    end
    
    return elapsed
end

-- 运行所有测试
local function run_all_tests()
    print(string.rep("=", 60))
    print(string.format("Lua解释器性能测试套件 (迭代次数: %d)", iterations))
    print("Lua 版本: " .. _VERSION)
    print("JIT: " .. (jit and "启用" or "禁用"))
    if jit then
        print("JIT 版本: " .. jit.version)
        print("JIT 架构: " .. jit.arch)
    end
    print(string.rep("=", 60))
    
    local total_time = 0
    local test_times = {}
    
    for name, test_info in pairs(tests) do
        if name ~= "recursion" then  -- 跳过默认运行递归测试
            local elapsed = run_test(name, test_info)
            test_times[name] = elapsed
            total_time = total_time + elapsed
        end
    end
    
    -- 显示总结
    print(string.rep("=", 60))
    print("测试总结:")
    print(string.rep("-", 60))
    
    -- 按时间排序
    local sorted_tests = {}
    for name, _ in pairs(test_times) do
        table.insert(sorted_tests, name)
    end
    table.sort(sorted_tests, function(a, b)
        return test_times[a] < test_times[b]
    end)
    
    for _, name in ipairs(sorted_tests) do
        print(string.format("%-20s: %.6f 秒", name, test_times[name]))
    end
    
    print(string.rep("-", 60))
    print(string.format("总计时间: %.6f 秒", total_time))
    print(string.format("平均每秒操作数: %.2f", iterations / (total_time / #sorted_tests)))
    
    -- 可选：运行递归测试（单独，因为较慢）
    print("\n可选递归测试 (Fibonacci(30), 可能较慢):")
    print("要运行请使用: lua benchmark.lua recursion")
end

-- 主程序
local function main()
    if test_name == "all" or test_name == "" then
        run_all_tests()
    elseif tests[test_name] then
        local result, elapsed = time_function(test_name, tests[test_name][1], tests[test_name][2])
        print(string.format("\n测试完成: %s", test_name))
        print(string.format("结果: %s", tostring(result)))
        print(string.format("时间: %.6f 秒", elapsed))
        
        if tests[test_name][2] and elapsed > 0 then
            print(string.format("性能: %.2f 次操作/秒", tests[test_name][2] / elapsed))
        end
    else
        print("可用测试:")
        for name, _ in pairs(tests) do
            print("  " .. name)
        end
        print("\n用法: lua benchmark.lua [测试名称] [迭代次数]")
        print("示例: lua benchmark.lua arithmetic 1000000")
        print("      lua benchmark.lua all")
    end
end

-- 运行
main()