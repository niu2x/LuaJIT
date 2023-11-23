w, h, z = 80, 22, 0
k = 0

function r(t)
    return 16 * math.sin(t) ^ 3
end

function s(t)
    return 13 * math.cos(t) - 5 * math.cos(2*t) - 2 * math.cos(3*t) - math.cos(4*t)
end

for y = -h, h do
    l = ""
    m = y / h
    for x = -w, w do
        n = x / w
        a = r(k + 2 * n)
        b = s(k + 2 * n)
        l = l .. (m * m + n * n > 0 and a * a + b * b < 200 and "*" or " ")
    end
    print(l)
    k = k + 0.02
end