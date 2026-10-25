# ar: the old form (ar lib.a file.o ...) and the keys r q t x d with v;
# a program linked with the archive
echo '        .text' > alpha.s
echo '        .global alpha' >> alpha.s
echo 'alpha:  li    r1, 7' >> alpha.s
echo '        ret' >> alpha.s
echo '        .text' > beta.s
echo '        .global beta, gamma' >> beta.s
echo 'beta:   li    r1, 1' >> beta.s
echo '        ret' >> beta.s
echo 'gamma:  li    r1, 2' >> beta.s
echo '        ret' >> beta.s
echo '        .text' > delta.s
echo '        .global delta' >> delta.s
echo 'delta:  call  alpha' >> delta.s
echo '        ret' >> delta.s
echo '        .text' > main.s
echo '        .global main' >> main.s
echo 'main:   call  delta' >> main.s
echo '        ret' >> main.s
as alpha.s
as beta.s
as delta.s
as main.s
echo '== the old form'
ar old.a beta.o alpha.o
echo "exit $?"
ar -t old.a
echo "exit $?"
echo '== r makes the same archive'
ar r new.a alpha.o beta.o
echo "exit $?"
cmp old.a new.a && echo 'same archive'
ar -r dash.a alpha.o beta.o
cmp old.a dash.a && echo 'same archive (-r)'
test -f ./-r || echo 'no file -r'
echo '== rv: add and replace; t, tv'
ar rv new.a delta.o alpha.o
echo "exit $?"
ar t new.a
ar t new.a beta.o
ar tv new.a alpha.o
echo '== q, d'
ar q q.a delta.o
ar t q.a
ar dv new.a beta.o
echo "exit $?"
ar t new.a
ar d new.a beta.o
echo "exit $? (not a member)"
echo '== x'
mkdir x
cd x
ar xv ../new.a
echo "exit $?"
cmp alpha.o ../alpha.o && cmp delta.o ../delta.o && echo 'extracted the same'
ls
rm alpha.o delta.o
ar x ../new.a delta.o
ls
cd ..
echo '== linking with the archive'
ld -o prog /usr/lib/crt0.o main.o new.a /usr/lib/libc.a
echo "exit $?"
./prog
echo "exit $?"
echo '== errors'
ar
echo "exit $?"
ar -z lib.a alpha.o
echo "exit $?"
ar new.a
echo "exit $?"
ar t nosuch.a
echo "exit $?"
ar t alpha.o
echo "exit $?"
ar r new.a alpha.s
echo "exit $?"
ar t new.a
