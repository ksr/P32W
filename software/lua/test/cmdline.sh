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
