# as: usage errors, one source at a time, errors in the source, -o, and a
# program assembled and linked here
echo '        .text' > hello.s
echo '        .global main' >> hello.s
echo 'main:   la    r1, msg' >> hello.s
echo '        call  puts' >> hello.s
echo '        li    r1, 3' >> hello.s
echo '        ret' >> hello.s
echo '        .data' >> hello.s
echo 'msg:    .asciz "hello, world"' >> hello.s
echo '== usage'
as
echo "exit $?"
as -x hello.s
echo "exit $?"
as hello.s hello.s
echo "exit $? (two sources)"
test -f hello.o || echo 'no hello.o'
as nosuch.s
echo "exit $?"
echo '== errors in the source'
echo '        .text' > bad.s
echo '        addx  r1, r2, r3' >> bad.s
echo '        li    r1, 3' >> bad.s
echo '        .bogus 1' >> bad.s
echo 'x:      nop' >> bad.s
echo 'x:      nop' >> bad.s
as bad.s
echo "exit $?"
test -f bad.o || echo 'no bad.o'
echo '== a program'
as hello.s
echo "exit $?"
as -o other.o hello.s
echo "exit $?"
cmp hello.o other.o && echo 'same object'
ld -o hello /usr/lib/crt0.o hello.o /usr/lib/libc.a
echo "exit $?"
./hello
echo "exit $?"
