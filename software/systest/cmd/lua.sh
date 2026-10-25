# lua: Lua 5.4 (software/lua/test, put together by mksh.sh): numbers
# (32-bit integers, double floats and their formatting, the maths
# library), strings (patterns, format, pack, utf8), the language (tables,
# closures, metatables, coroutines, errors, goto, load), io and os
# (files, seek, popen, dates, tmpfile, require), then the lua command's
# options, its REPL and luac
cat > numbers.lua <<'LUAEOF'
-- numbers.lua: integers (32 bits on P32W) and floats (IEEE double)
print("== integers and floats")
print(3 // 2, 3.0 // 2, -3 // 2, 3 / 2, 4 / 2, 2^0.5, 2^10, 7 % -3, -7 % 3, 7.5 % 2, -7.5 % 2)
print(math.type(1), math.type(1.0), math.type("1"), math.type(2^31))
print(math.maxinteger, math.mininteger, math.maxinteger + 1 == math.mininteger)
print(math.maxinteger * 2, math.mininteger // -1, math.mininteger % -1)
print(1 == 1.0, 2^53 == 2^53 + 1, math.ult(1, -1), 0x7fffffff, 0xffffffff, 0x10)
print(5 // 0.0, -5 // 0.0, 0/0 ~= 0/0, pcall(function () return 5 // 0 end))
print(pcall(function () return 5 % 0 end))
print(7 & 3, 7 | 8, 7 ~ 2, ~0, 1 << 31, 1 << 32, -1 >> 1, 2.0 & 3)
print(pcall(function () return 1.5 & 1 end))
print(math.tointeger(3.0), math.tointeger(3.5), math.tointeger("8"), 3 | 0)
print(string.format("%d", 3.0), pcall(string.format, "%d", 3.5))
print(10 == "10", "10" + 1, "0x10" + 0, "1e2" * 1, " 5 " * 2, 10 .. 20)
print(tonumber("  0x1p4  "), tonumber("1e1"), tonumber("z", 36), tonumber("777", 8), tonumber("1.5e"), tonumber(""))
print(tonumber("0x7fffffff"), tonumber("0xffffffff"), tonumber("2147483648"), tonumber("-2147483649"))
print(2147483647 + 0, 2147483648, -2147483648, math.type(2147483648))

print("== float formatting")
print(0.1, 0.1 + 0.2, 1/3, -1/3, 2/3, 1e100, 1e-100, 123456789012345.0)
print(1e15, 1e16, 2^53, 2^63, -2^63, 100.0, -0.0, 0.0, 1e300 * 1e10, -1e300 * 1e10)
print(3.14159265358979, math.pi, -math.pi, 1e-5, 0.0001, 5e-324, 1.7976931348623157e308)
print(255 / 5, 7.0, 1e2, 12.5e-1, .5, 5., 0x.8, 0xA.8p1, 0x1p-2)
print(string.format("%5.2f|%-8.3e|%g|%g|%g|%G", math.pi, 12345.6789, 1e20, 1e-5, 123456789, 1e-10))
print(string.format("%.0f %.0f %.0f %.0f %.1f %.2f", 0.5, 1.5, 2.5, -0.5, 0.05, 1.005))
print(string.format("%010.4f|%+.3g|% g|%#g|%#.0f|%.3e", -3.14159, 2.5, 7.0, 1.0, 2.0, 0.0))
print(string.format("%a %A %.3a %a %a", 1.0, 0.1, 1/3, -0.0, 2^-1074))
print(string.format("%.20f", 0.1))
print(string.format("%.17g %.17g %.17g", 0.1, 1/3, 2/3))
print(string.format("%.99f", 1/3):sub(1, 40), #string.format("%99.99f", -1e308))
print(string.format("%q", 1/3), string.format("%q", 1/0), string.format("%q", -1/0), string.format("%q", 0/0))
print(string.format("%q", math.mininteger), string.format("%q", 255), string.format("%q", 2^63))
print(string.format("%5s|%-5s|%.2s|%c%c%c", "ab", "cd", "xyz", 76, 117, 97))
print(string.format("%5d|%-5d|%05d|%x|%X|%o|%i|%+d", 42, 42, 42, 255, 255, 8, -3, 3))
print(tostring(1e15), tostring(-1e15), tostring(1e14), 2^24, 2^31, 2^32)
print(8 // 3 * 3 + 8 % 3, 2^-1, (-2)^2, -2^2, 2^3^2)

print("== conversions")
print(math.floor(3.7), math.floor(-3.7), math.ceil(3.2), math.ceil(-3.2), math.floor(2^40), math.ceil(-2^40))
print(math.floor(-0.0), math.ceil(-0.5), math.fmod(7, 3), math.fmod(-7, 3), math.fmod(7, -3), math.fmod(-7.5, 2))
print(math.fmod(1e300, 3), math.fmod(5.5, 1/0), math.fmod(3, 0.0), pcall(math.fmod, 3, 0))
print(math.abs(-3), math.abs(-3.5), math.abs(math.mininteger), math.max(1, 5.5, 3), math.min(4, -2, 0))
print(math.modf(3.7), math.modf(-3.7), math.modf(1/0), math.modf(5))
print(3.7 // 1, -3.7 // 1, 3 % math.huge, -3 % math.huge, 5.25 % -2)
print(string.rep("=", 3), 1e308 * 10 == math.huge, -math.huge < math.mininteger)
print(1 < 1.5, 2^53 < 2^53 + 1, math.maxinteger < 2^31, math.maxinteger + 0.0 == 2^31 - 1)
print(1//0.0 == math.huge, math.mininteger <= -2^31, 3 == 3.0000000000000001)

print("== math functions (15 digits)")
local function f(x) return string.format("%.15g", x) end
print(f(math.sqrt(2)), f(math.sqrt(1e10)), f(math.exp(1)), f(math.exp(-1)), f(math.log(10)))
print(f(math.log(8, 2)), f(math.log(1000, 10)), f(math.log(100, 4)), f(math.log(0.5, 2)), math.log(1024, 2))
print(f(math.sin(1)), f(math.cos(1)), f(math.tan(1)), f(math.sin(math.pi)), f(math.cos(math.pi)))
print(f(math.asin(0.5)), f(math.acos(0.5)), f(math.atan(1)), f(math.atan(1, -1)), f(math.atan(-1, -1)))
print(f(math.atan(0, -1)), f(math.atan(-0.0, -1)), f(math.atan(1, 0)), f(math.atan(1e300)))
print(f(2^0.5), f(10^-2), f(2^-1074), f(1.5^100), f((-8)^(1/3)), f(0^0), f(math.exp(709)))
print(math.log(0), math.log(-1) ~= math.log(-1), math.sqrt(-1) ~= math.sqrt(-1), math.exp(1000), math.exp(-1000))
print(math.huge, -math.huge, math.pi, f(math.rad(180)), f(math.deg(math.pi / 3)))

print("== random (seeded)")
math.randomseed(42)
local t = {}
for i = 1, 8 do t[#t + 1] = math.random(1, 100) end
print(table.concat(t, " "))
print(math.random(5, 5), pcall(math.random, 2, 1))
local r = math.random(); print(r >= 0 and r < 1, math.type(math.random(10)), math.type(r))
math.randomseed(42); print(math.random(1, 100) == t[1])
LUAEOF
lua numbers.lua 2>&1
echo "status $?"
cat > strings.lua <<'LUAEOF'
-- strings.lua: the string library, patterns, pack/unpack, utf8
print("== basics")
local s = "Hello, P32W World"
print(#s, s:len(), s:upper(), s:lower(), s:reverse(), s:sub(8, 10), s:sub(-5), s:sub(0), s:sub(10, 2))
print(s:byte(1), s:byte(-1), s:byte(1, 3), string.char(72, 105), ("x"):rep(5), ("ab"):rep(3, ","), ("x"):rep(0))
print(s:find("P32W"), s:find("o", 6), s:find("xyz"), s:find("l+"), s:find(".", 1, true), s:find("", 20))
print(s:match("(%a+), (%w+)"), s:match("%d+"), s:match("^Hello"), s:match("World$"), s:match("()o()"))
print(("key = value"):match("^(%w+)%s*=%s*(%w+)$"))
print(string.format("[%s]", ("  trim me  "):match("^%s*(.-)%s*$")))
print(("hello world from lua"):gsub("o", "0"))
print(("hello world"):gsub("(%w+)", "<%1>"))
print(("hello world"):gsub("%w+", string.upper, 1))
print(("$name is $age"):gsub("%$(%w+)", { name = "Ken", age = 42 }))
print(("abc"):gsub("", "-"))
print(("one two  three"):gsub("%s+", " "))
print(("f(a(b)c)d"):match("%b()"), ("THE (quick) fox"):find("%f[%a]%a+%f[%A]"), ("aaa"):match("a-b"), ("aaab"):match("a-b"))
print(("x=1, y=22, z=333"):gsub("(%w)=(%d+)", "%2=%1"))
print(pcall(string.gsub, "x", "(", "y"))
print(pcall(string.rep, "x", -1), ("%d"):format(7), ("%5.1s|"):format("abc"))
local words = {}
for w in ("the quick brown fox"):gmatch("%a+") do words[#words + 1] = w end
print(#words, table.concat(words, "|"))
for k, v in ("a=1, b=2, c=3"):gmatch("(%w+)=(%w+)") do io.write(k, ":", v, " ") end
print()
print(("abc"):byte(10), ("\0a\0"):len(), ("a\0b"):find("\0", 1, true), ("A"):lower() < ("b"):lower(), "a" < "b", "Z" < "a", "abc" < "abd", "" < "a")
print(("%q"):format('he said "hi"\n\tand\0left\200'))
print(string.format("%-10s|%10s|", "left", "right"), tostring(nil), tostring(true), tostring(12))
print(("CamelCaseString"):gsub("(%u)", function (c) return "_" .. c:lower() end))
print(("192.168.1.1"):match("^(%d+)%.(%d+)%.(%d+)%.(%d+)$"))
print(("[[nested]]"):find("[[", 1, true), ("a.b.c"):gsub("%.", "/"), ("%"):rep(3))

print("== pack and unpack")
local p = string.pack("i4i2Bz", 100000, -2, 255, "hi")
print(#p, string.unpack("i4i2Bz", p))
print(string.unpack("<I2", "\1\2"), string.unpack(">I2", "\1\2"), string.packsize("i4i8d"), string.packsize("!8i1i8"))
print(string.unpack("d", string.pack("d", 1/3)) == 1/3, string.unpack("<f", string.pack("<f", 0.5)))
print(pcall(string.pack, "i4", 2^40), string.pack(">s1", "abc"):byte(1, -1))
print(string.unpack("<i8", string.pack("<i8", -5)), pcall(string.unpack, "<i8", ("\255"):rep(7) .. "\127"))
print(string.unpack("<j", string.pack("<j", math.mininteger)), string.packsize("j"), string.packsize("n"))
local q = string.pack("<I3 x xx I1", 0x123456, 7)
print(#q, string.unpack("<I3 xxx I1", q))

print("== utf8")
local u = "héllo wörld €𝄞"
print(#u, utf8.len(u), utf8.char(72, 233, 8364, 119070), utf8.codepoint(u, 1, 3))
for p2, c in utf8.codes("aé€") do io.write(p2, ":", c, " ") end
print()
print(utf8.offset(u, 3), utf8.offset(u, -1), utf8.len("\xff"), utf8.charpattern)
print(pcall(utf8.codepoint, "\xff"), utf8.len(u, 3), utf8.char(), utf8.char(0x7FFFFFFF):byte(1, -1))
local n = 0
for _ in u:gmatch(utf8.charpattern) do n = n + 1 end
print(n)
LUAEOF
lua strings.lua 2>&1
echo "status $?"
cat > lang.lua <<'LUAEOF'
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
LUAEOF
lua lang.lua 2>&1
echo "status $?"
cat > io.lua <<'LUAEOF'
-- io.lua: files (write, read back, lines, seek), the os library, modules
print("== io")
local name = "iotest.txt"
local f = assert(io.open(name, "w"))
print(io.type(f), io.type(io.stdout), io.type(42))
f:write("line one\n", "line two\n", 3, " ", 4.5, "\n", "last line without newline")
f:close()
print(io.type(f), pcall(f.write, f, "x"))
for l in io.lines(name) do io.write("[", l, "] ") end
print()
for l in io.lines(name, "L") do io.write(#l, " ") end
print()
f = assert(io.open(name, "r"))
print(f:read("l"), f:read("n"), f:read("n"), f:read("l"), f:read("a"), f:read("a"), f:read("l"))
print(f:seek("set", 5), f:read(3), f:seek("cur"), f:seek("end"), f:read(1))
print(f:seek("set"), f:read("L"), f:read(0), f:seek("cur", -4), f:read("l"))
f:close()
f = assert(io.open(name, "a+"))
f:write("\nappended")
f:seek("set")
local all = f:read("a")
print(#all, select(2, all:gsub("\n", "")), all:sub(-8))
f:close()
f = assert(io.open(name, "r+"))
f:write("LINE")
f:seek("set")
print(f:read("l"))
f:close()
f = io.open(name, "rb"); print(f:read(4)); f:close()
local g = assert(io.open("nums.txt", "w"))
g:write("10 20.5 0x1F -3e2 abc\n")
g:close()
g = io.open("nums.txt")
print(g:read("n", "n", "n", "n", "n"))
g:close()
print(io.open("no/such/file"))
print(pcall(io.lines, "no/such/file"))
print(pcall(io.open, name, "rw"))
io.output("out.txt")
io.write("via io.write\n")
io.output():close()
io.output(io.stdout)
io.input("out.txt"); print(io.read("L")); io.input():close(); io.input(io.stdin)
local tf = io.tmpfile()
tf:write("temporary"); tf:seek("set"); print(tf:read("a")); tf:close()
print(os.remove(name), os.remove("nums.txt"), select(2, os.remove("nonexistent")))
print(os.rename("out.txt", "renamed.txt"), io.open("renamed.txt"):read("a"))
os.remove("renamed.txt")
local p = io.popen("echo from a pipe; echo second")
print(p:read("a")); print(p:close())
local pw = io.popen("wc -c > wc.out", "w"); pw:write("12345"); print(pw:close())
print(io.open("wc.out"):read("n")); os.remove("wc.out")
print(io.stdout:setvbuf("full"), io.stdout:setvbuf("line"), io.stdout:setvbuf("no"))
io.stdout:setvbuf("line")
io.stderr:write("")

print("== os")
print(math.type(os.time()), os.time() > 1700000000, type(os.clock()), os.clock() >= 0)
print(os.time({ year = 2026, month = 10, day = 14, hour = 12, min = 30, sec = 0, isdst = false }) - os.time({ year = 2026, month = 10, day = 13, hour = 12, min = 30, sec = 0, isdst = false }))
print(os.date("!%Y-%m-%d %H:%M:%S", 0), os.date("!%c", 86400 * 365), os.date("!%x %X %p", 1000000000))
print(os.date("!%A %a %B %b %d %e %j %U %W %u %w %y %C %G %g %V %%", 1791936000))
print(os.date("!%D %F %R %T %r %I %h %n|%t|", 1791972000))
local d = os.date("!*t", 1791936000)
print(d.year, d.month, d.day, d.hour, d.min, d.sec, d.wday, d.yday, d.isdst)
print(os.date("!%H:%M", 1791936000 + 3600 * 13 + 60 * 7), pcall(os.date, "%Ez"), pcall(os.date, "%Q"))
local now = os.time(); local t = os.date("*t", now); print(os.time(t) == now)
print(os.difftime(10, 4), os.getenv("HOME") ~= nil, os.getenv("NO_SUCH_VARIABLE"))
local tn = os.tmpname(); print(type(tn), io.open(tn) ~= nil); os.remove(tn)
print(os.execute(), os.execute("exit 3"), os.execute("true"))
print(os.setlocale(), os.setlocale("C"), os.setlocale("fr_FR"), os.setlocale(nil, "numeric"))

print("== modules")
local m = io.open("mymod.lua", "w")
m:write("local M = {}\nfunction M.greet(n) return 'hello ' .. n end\nM.loaded_as = ...\nreturn M\n")
m:close()
package.path = "./?.lua;" .. package.path
local mod = require("mymod")
print(mod.greet("P32W"), mod.loaded_as, require("mymod") == mod, package.loaded.mymod == mod)
print(pcall(require, "no_such_module"))
print(type(package.searchers), package.config:sub(1, 1), type(package.preload))
package.preload.virtual = function (n) return { name = n } end
print(require("virtual").name)
local chunk = loadfile("mymod.lua"); print(type(chunk), chunk("x").loaded_as)
print(dofile("mymod.lua").greet("again"))
os.remove("mymod.lua")
LUAEOF
lua io.lua 2>&1
echo "status $?"
echo "== the lua command"
echo 'print("script args", #arg, arg[0], arg[1], arg[2], arg[-1] ~= nil, ...)' > args.lua
echo 'return { answer = 42 }' > m42.lua
LUA_INIT='print("LUA_INIT ran")' lua -v -l m42 -e 'print("-l", m42.answer)' -e 'x = 10' args.lua one two
echo 'print("from stdin", 6 * 7, x)' | lua -e 'x = 1' -
lua -e 'os.exit(3)'; echo "exit $?"
lua -W -e 'warn("a warning")' -e 'error("boom")' 2>&1; echo "exit $?"
lua no_such_script.lua 2>&1; echo "exit $?"
echo 'x = ' > bad.lua
lua bad.lua 2>&1; echo "exit $?"
echo "== the REPL (lua -i, input from a here-document)"
lua -i 2>&1 <<'EOF'
x = 5
x * 2
for i = 1, 2 do
print("i", i)
end
= x + 1
local y = 3; print(y)
error("in repl")
"a" .. 1
EOF
echo "== luac"
cat > fact.lua <<'EOF'
local function fact(n) if n <= 1 then return 1 end return n * fact(n - 1) end
print(fact(12), fact(5) / 2, 2^0.5, "done")
EOF
luac -o fact.luac fact.lua; echo "luac $?"
lua fact.luac
luac -s -o - fact.lua | lua -
luac -p bad.lua 2>&1; echo "exit $?"
luac -l -l fact.lua | sed -e 's/ at 0x[0-9a-f]*//' -e 's/	; 0x[0-9a-f]*$//' -e 's/ for 0x[0-9a-f]*:/:/'
