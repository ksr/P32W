# vi, driven by keys from a file (the screen goes to /dev/null): . after a
# yank, ex ranges, a failed :s, a partial :w, end of input with changes
HOME=.
for n in 1 2 3 4 5 6 7 8 9 10; do echo line$n >> t; done
echo '== dd yy . : the . repeats dd, not the yank'
cp t a
echo -n 'ddyy.' > k
echo ':wq' >> k
vi a < k > /dev/null
echo "exit $?"
cat a
echo '== bad ranges change nothing; :3,4d and :0r work'
cp t b
echo one > one
echo ':5,3d' > k
echo ':100d' >> k
echo ':9,11d' >> k
echo ':3,4d' >> k
echo ':0r one' >> k
echo ':wq' >> k
vi b < k > /dev/null
echo "exit $?"
cat b
echo '== a failed :s leaves the buffer unchanged, so :q quits'
cp t c
echo ':%s/zzz/y/' > k
echo ':q' >> k
vi c < k > /dev/null
echo "exit $?"
test -f vi.recover && echo recovered
cmp t c && echo same
echo '== :1,2w writes part, so :q refuses; end of input saves vi.recover'
cp t d
echo -n 'x' > k
echo ':1,2w' >> k
echo ':q' >> k
vi d < k > /dev/null
echo "exit $?"
cat d
cat vi.recover
