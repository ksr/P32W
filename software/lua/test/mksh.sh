#!/bin/sh
# mksh.sh -- make ../../systest/cmd/lua.sh, P32W Unix's test of Lua, from the
# tests here: each .lua file goes in as a here-document and is run, then
# cmdline.sh (the lua and luac commands). Run it here after changing a test;
# the .expect beside lua.sh is recorded on P32W (README.P32W).
cd "$(dirname "$0")" || exit 1
out=../../systest/cmd/lua.sh
{
    echo "# lua: Lua 5.4 (software/lua/test, put together by mksh.sh): numbers"
    echo "# (32-bit integers, double floats and their formatting, the maths"
    echo "# library), strings (patterns, format, pack, utf8), the language (tables,"
    echo "# closures, metatables, coroutines, errors, goto, load), io and os"
    echo "# (files, seek, popen, dates, tmpfile, require), then the lua command's"
    echo "# options, its REPL and luac"
    for t in numbers strings lang io; do
        echo "cat > $t.lua <<'LUAEOF'"
        cat $t.lua
        echo "LUAEOF"
        echo "lua $t.lua 2>&1"
        echo "echo \"status \$?\""
    done
    cat cmdline.sh
} > $out
echo "$out: $(wc -l < $out) lines"
