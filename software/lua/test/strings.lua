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
