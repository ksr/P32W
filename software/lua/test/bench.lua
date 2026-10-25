-- bench.lua: rough speed figures (os.clock: CPU seconds)
local function time(name, f)
  local t0 = os.clock()
  local r = f()
  print(string.format("%-28s %8.2f s   %s", name, os.clock() - t0, tostring(r)))
end
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
time("fib(25)", function () return fib(25) end)
time("loop 100000 (integer +)", function () local s = 0; for i = 1, 100000 do s = s + i end return s end)
time("loop 100000 (float *)", function () local s = 1.0; for i = 1, 100000 do s = s * 1.000001 end return s end)
time("string concat 10000", function () local t = {}; for i = 1, 10000 do t[#t + 1] = tostring(i) end return #table.concat(t) end)
time("table insert/sort 10000", function () local t = {}; math.randomseed(1); for i = 1, 10000 do t[i] = math.random(100000) end; table.sort(t); return t[1] end)
time("string.format %.14g 2000", function () local s; for i = 1, 2000 do s = string.format("%.14g", i / 7) end return s end)
