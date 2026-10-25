-- lang.lua: tables, closures, metatables, coroutines, errors, goto, varargs
print("== tables")
local t = { 10, 20, 30, x = 1, ["y z"] = 2, [3.0] = "three", [-1] = "neg" }
print(#t, t[3], t.x, t["y z"], t[-1], rawlen(t), next({}))
t[#t + 1] = 40; table.insert(t, 1, 5); table.insert(t, 25)
print(#t, table.concat(t, ","), table.remove(t), table.remove(t, 1), table.concat(t, ","))
local u = { 5, 2, 8, 1, 9, 3 }
table.sort(u); print(table.concat(u, " "))
table.sort(u, function (a, b) return a > b end); print(table.concat(u, " "))
local names = { "pear", "Apple", "fig", "banana" }
table.sort(names); print(table.concat(names, " "))
print(table.unpack({ 1, 2, 3 }), select("#", table.unpack({ 1, nil, 3 }, 1, 3)), table.pack(1, nil, 3).n)
print(table.concat(table.move({ 1, 2, 3, 4, 5 }, 2, 4, 1), ","), table.concat({}, ","), select(-1, "a", "b"))
local keys = {}
for k in pairs({ a = 1, b = 2, c = 3, d = 4 }) do keys[#keys + 1] = k end
table.sort(keys); print(table.concat(keys))
local sum = 0
for i, v in ipairs({ 1, 2, 3, nil, 5 }) do sum = sum + i * v end
print(sum)
local big = {}
for i = 1, 3000 do big[i] = i * i end
print(#big, big[3000], big[1500] // 1000)
local h = {}
for i = 1, 500 do h["k" .. i] = i end
local n = 0
for k, v in pairs(h) do n = n + v end
print(n, h.k499)
print(pcall(table.insert, {}, 1, 2, 3), pcall(function () local x = nil; return x.y end))

print("== closures")
local function counter()
  local c = 0
  return function () c = c + 1; return c end
end
local c1, c2 = counter(), counter()
print(c1(), c1(), c1(), c2())
local fs = {}
for i = 1, 3 do fs[i] = function () return i * 10 end end
print(fs[1](), fs[2](), fs[3]())
local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
print(fib(15))
local function va(...) return select("#", ...), ... end
print(va(), va(nil), va(1, nil, nil), (va(1, 2, 3)))
local function tail(n) if n == 0 then return "done" end return tail(n - 1) end
print(tail(5000))
print(select(2, pcall(function () local function r() return 1 + r() end return r() end)):match("stack overflow"))
print(string.rep("x", 3, "-"), (function (...) local a, b = ... return b end)(1, 2))

print("== metatables")
local V = {}
V.__index = V
V.__add = function (a, b) return setmetatable({ x = a.x + b.x, y = a.y + b.y }, V) end
V.__eq = function (a, b) return a.x == b.x and a.y == b.y end
V.__tostring = function (v) return "(" .. v.x .. "," .. v.y .. ")" end
V.__lt = function (a, b) return a.x < b.x end
V.__le = function (a, b) return a.x <= b.x end
V.__len = function () return 2 end
V.__call = function (self, k) return self[k] end
V.__concat = function (a, b) return tostring(a) .. tostring(b) end
V.__unm = function (a) return setmetatable({ x = -a.x, y = -a.y }, V) end
function V.new(x, y) return setmetatable({ x = x, y = y }, V) end
function V:len2() return self.x * self.x + self.y * self.y end
local a, b = V.new(1, 2), V.new(3, 4)
print(tostring(a + b), a == V.new(1, 2), a < b, b <= a, #a, a("y"), a .. b, tostring(-a), b:len2())
local log = {}
local proxy = setmetatable({}, { __index = function (_, k) return k .. "!" end,
  __newindex = function (_, k, v) log[#log + 1] = k .. "=" .. tostring(v) end })
proxy.a = 1; proxy.b = nil
print(proxy.foo, rawget(proxy, "a"), table.concat(log, " "))
local ro = setmetatable({}, { __index = { x = 5 }, __metatable = "locked" })
print(ro.x, getmetatable(ro), pcall(setmetatable, ro, {}))
print(getmetatable("abc").__index == string, ("x"):rep(2))
local closed = {}
do
  local x <close> = setmetatable({}, { __close = function () closed[#closed + 1] = "closed" end })
  local y <const> = 5
end
print(closed[1])
local wk = setmetatable({}, { __mode = "k" })
wk[{}] = 1; collectgarbage(); collectgarbage()
print(next(wk))
local gcran = false
do local z = setmetatable({}, { __gc = function () gcran = true end }) end
collectgarbage(); collectgarbage(); print(gcran)
print(type(collectgarbage("count")), collectgarbage("step", 0) ~= nil, collectgarbage("isrunning"))

print("== coroutines")
local co = coroutine.create(function (x, y)
  print("start", x, y)
  local z = coroutine.yield(x + y)
  print("got", z)
  local w, v = coroutine.yield(z * 2)
  return w + v
end)
print(coroutine.resume(co, 1, 2))
print(coroutine.status(co), coroutine.resume(co, 10))
print(coroutine.resume(co, 3, 4))
print(coroutine.status(co), coroutine.resume(co))
local gen = coroutine.wrap(function () for i = 1, 3 do coroutine.yield(i) end end)
print(gen(), gen(), gen())
local function perm(a, n)
  n = n or #a
  if n <= 1 then coroutine.yield(a) else
    for i = 1, n do a[n], a[i] = a[i], a[n]; perm(a, n - 1); a[n], a[i] = a[i], a[n] end
  end
end
local count = 0
for p in coroutine.wrap(function () perm({ 1, 2, 3, 4 }) end) do count = count + 1 end
print(count, coroutine.isyieldable(), type(coroutine.running()))
local co2 = coroutine.create(function () error("oops") end)
print(coroutine.resume(co2))
print(coroutine.close(coroutine.create(print)), select("#", coroutine.running()))

print("== errors")
print(pcall(error, "msg"), pcall(error, "msg", 0), type(select(2, pcall(error, { code = 42 }))))
print(select(2, pcall(error)), pcall(error, nil))
local ok, e = pcall(function () error({ code = 7 }) end); print(ok, e.code)
print(xpcall(function () error("deep") end, function (m) return "handled: " .. m end))
print(pcall(function () return nil + 1 end))
print(pcall(function () return {} < {} end))
print(pcall(function () return #nil end))
print(pcall(function () local t = setmetatable({}, { __index = function () error("idx") end }); return t.x end))
print(select("#", pcall(error)), (pcall(string.rep)), string.format("%s", select(2, pcall(string.rep))))
local ok2, tb = xpcall(function () local x = nil; x() end, debug.traceback)
print(ok2, tb:match("^[^\n]*"))
print(pcall(load, "x = "), load("return 1 +"))
print(load("syntax error here"))
error_obj = setmetatable({}, { __tostring = function () return "custom error" end })
print(select(2, pcall(error, error_obj)) == error_obj)
print(tostring(nil), tostring(1.5), assert(1, "x"), pcall(assert, false), pcall(assert, nil, "boom"))

print("== goto and loops")
for i = 1, 3 do
  for j = 1, 3 do
    if j == 2 then goto continue end
    io.write(i, j, " ")
    ::continue::
  end
end
print()
do
  local k = 0
  ::top::
  k = k + 1
  if k < 5 then goto top end
  print("k", k)
end
local w = 0
while true do w = w + 1; if w > 10 then break end end
local r = 0
repeat local q = r; r = r + 1 until q >= 3
print(w, r)
for i = 10, 1, -3 do io.write(i, " ") end
for i = 1.0, 2.0, 0.25 do io.write(i, " ") end
for i = math.maxinteger - 2, math.maxinteger do io.write(i, " ") end
print()
for i = 1, 0 do print("never") end
print(pcall(function () for i = 1, 10, 0 do end end))

print("== load and env")
local env = { print = print, x = 5 }
local fn = load("x = x + 1; return x", "chunk", "t", env)
print(fn(), env.x, x)
local parts = { "return ", "1 ", "+ 41" }
local i = 0
print(load(function () i = i + 1; return parts[i] end)())
local dumped = string.dump(function (a) return a * 3 end)
print(type(dumped), load(dumped, "d", "b")(14), pcall(load, dumped, "d", "t"))
print(_VERSION, _G._G == _G, type(_ENV), rawequal(_G, _ENV))
print(#arg, arg[0] ~= nil)
