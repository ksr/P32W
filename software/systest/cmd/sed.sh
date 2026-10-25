# sed: addresses, every command, s flags, BRE and ERE, hold space, -n -e -f
# -E -s -i, several files, no final newline, long lines, errors and statuses
cat > in <<'EOF'
one
two
three
four
five
EOF
echo "== no commands"
sed '' in
sed -n '' in
echo "status $?"
echo "== p d and -n"
sed -n p in
sed 2d in
sed -n '$p' in
sed 2,4d in
sed '2!d' in
sed -n '$!p' in
echo "== addresses"
sed -n '/t/p' in
sed -n '/^t/,/^f/p' in
sed -n '2,/e/p' in
sed -n '/one/,3p' in
sed -n '3,1p' in
sed -n '1~2p' in
sed -n '0~2p' in
sed -n '2,+1p' in
sed -n '2,~4p' in
sed '0,/o/d' in
sed '1,/o/d' in
sed -n '\%f%p' in
sed -n '\,^t,p' in
sed -n '/O/Ip' in
sed -n '$=' in
sed -n '/e$/=' in
echo "== s"
sed 's/o/0/' in
echo foo boo | sed 's/o/0/g'
echo eeee | sed 's/e/E/2'
echo eeee | sed 's/e/E/3g'
sed -n 's/e/E/2p' in
echo hello world | sed 's/\(hello\) \(world\)/\2 \1/'
echo hello | sed 's/l/[&]/g'
echo hello | sed 's/l/\&/'
echo a/b/c | sed 's/\//|/g'
echo a/b/c | sed 's|/|\\|g'
echo a,b | sed 's,\,,;,'
echo abc | sed 's/b/\n/'
cat > s <<'EOF'
s/b/\
/
EOF
echo abc | sed -f s
echo 'a b' | sed 's/ /\t/' | sed -n l
echo Hello | sed 's/hello/bye/I'
echo Hello | sed 's/hello/bye/'
echo 'one two' | sed 's/\(o[a-z]*\) \(t[a-z]*\)/\U\2\E-\u\1/'
echo 'ONE TWO' | sed -E 's/(\w+)/\L&/2'
echo "== empty matches"
echo abc | sed 's/x*/-/g'
echo abc | sed 's/b*/X/g'
echo abc | sed 's/b*/X/2'
echo baaac | sed 's/a*/X/g'
echo aaa | sed 's/a*/X/g'
echo abc | sed 's/$/!/'
echo abc | sed 's/^/>/g'
echo hello | sed 's/l*$/./'
echo "== s///p, w and the t flag"
sed -n 's/o/O/p' in
sed -n 's/e/E/gw out' in
cat out
sed 's/^t/T/;t;s/$/ -/' in
sed 's/^t/T/;T;s/$/ +/' in
echo 1234567 | sed ':a;s/\([0-9]\)\([0-9]\{3\}\)\($\|,\)/\1,\2\3/;ta'
echo "== regular expressions"
cat > r <<'EOF'
abc123
ABC def
a.b*c
x  y	z
aaa
abab
]br[a]
*star
EOF
sed -n '/[[:digit:]]/p' r
sed -n '/^[[:upper:]]\{3\}/p' r
sed -n '/[[:space:]]/p' r
sed -n '/[[:punct:]]/p' r
sed -n '/^[a-c]*$/p' r
sed -n '/a\.b\*c/p' r
sed -n '/\(ab\)\1/p' r
sed -n '/^\(.\)\1\1$/p' r
sed -n '/a\{3\}/p' r
sed -n 's/a\{1,2\}/X/p' r
sed -n '/^*/p' r
sed -n '/^\*/p' r
sed -n '/b*c/p' r
sed -n 's/[^a-z]/_/gp' r
sed -n '/[]]/p' r
sed -n '/[^]a-z[]/p' r
sed -n 's/[[:alpha:]][[:alnum:]]*/<&>/gp' r
sed -n '/^[[:lower:][:digit:]]*$/p' r
sed -n '/c$/p' r
sed -n '/a$b/p' r
echo 'a$b' | sed -n '/a$b/p'
echo 'a^b' | sed 's/a^b/ok/'
echo 'x|y' | sed 's/x|y/ok/'
echo 'xy' | sed 's/x\|y/-/g'
echo 'xxy' | sed 's/x\+/+/'
echo 'y' | sed 's/x\?y/?/'
echo 'abcabc' | sed 's/\(a\)\(b\)\(c\)\1\2\3/[\3\2\1]/'
echo 'tab	here' | sed -n '/\t/p'
echo 'word, words' | sed 's/\<w/W/g'
echo 'word, words' | sed 's/s\>/S/g'
echo "== extended regular expressions"
sed -E -n '/^(ab)+$/p' r
sed -r -n '/^a{3}$/p' r
sed -E 's/a|b/X/g' r
sed -E -n 's/(a|b)+/<&>/p' r
echo 'f(x)' | sed -E 's/\(x\)/[x]/'
echo 'xxyz' | sed -E 's/x+/+/;s/y?z/?/'
echo abcd | sed -E 's/a|ab/X/'
echo xyz | sed -E 's/x(y?)(yz)?/[\1|\2]/'
echo 'a+b' | sed 's/a+b/plus/'
echo 'aaa' | sed -E 's/a{2}/2/'
echo 'aaaa' | sed -E 's/a{1,3}/X/g'
echo 'ab ab' | sed -E 's/(ab) \1/twice/'
echo "== y = l"
echo hello | sed 'y/elo/ELO/'
echo 'a/b' | sed 'y,/,|,'
echo 'tab	back\slash' | sed -n l
echo 0123456789012345678901234567890123456789012345678901234567890123456789012345 | sed -n l
echo 0123456789 | sed -n 'l 5'
echo 0123456789012345678901234567890123456789012345678901234567890123456789012345 | sed -n 'l 0'
sed -n '2{=;l}' in
echo "== a i c"
cat > s <<'EOF'
2a\
after two
EOF
sed -f s in
cat > s <<'EOF'
2i\
before two
EOF
sed -f s in
sed '$a the end' in
sed '1i the start' in
cat > s <<'EOF'
2,3c\
changed
EOF
sed -f s in
sed -e '2,3!c\' -e other in
sed -e '/^t/c\' -e 'was t' in
sed -e '1a\' -e 'two lines\' -e 'of text' in
sed '3a\  indented' in
cat > s <<'EOF'
1{i\
first
a\
second
}
EOF
sed -f s in
echo "== q Q"
sed 2q in
echo "status $?"
sed '3q7' in
echo "status $?"
sed 3Q in
echo "status $?"
sed -n '2{p;q}' in
sed -e '1a\' -e appended -e 1q in
echo "== n N D P"
sed n in
sed -n 'n;p' in
sed '$!N;s/\n/+/' in
sed 'N;s/\n/+/' in
sed '$!N;P;D' in
sed -n '$!N;P;D' in
sed '1{N;N;D}' in
sed ':a;N;$!ba;s/\n/,/g' in
sed -n 'N;l;D' in
sed -n '$!N;/e\nt/p' in
sed 'N;y/\n/ /' in
sed '$!N;s/^\(.*\)\n\1$/same/' in
echo "== hold space"
sed -n 'h;n;G;p' in
sed -n 'H;${x;s/\n/,/g;p}' in
sed '1!G;h;$!d' in
sed -n '1h;1!H;${g;s/\n/ /gp}' in
sed 'x' in
sed 'G' in | wc -l
sed -n 'x;p;x;p' in | head -4
sed 'z;s/^$/empty/' in | head -2
echo "== branches and blocks"
sed -n '/t/{/o/p}' in
sed -n '/t/!{/o/p}' in
sed '/two/b;s/$/./' in
sed -n '/two/bx;p;:x' in
sed -e ':top' -e 's/^o//;t top' in
sed -n '2,4{/h/!p}' in
sed -n '{p};{p}' in | wc -l
sed -n '/one/{n;p}' in
sed '2{h;d};4{G}' in
echo "== comments and ;"
cat > s <<'EOF'
# a comment
/one/p   # another
2p;3p ; 4p
EOF
sed -n -f s in
cat > script <<'EOF'
#n
# print the lines with an o, numbered
/o/{
=
p
}
EOF
sed -f script in
cat > script2 <<'EOF'
s/one/1/
2a\
after\
two
$i\
before five
EOF
sed -f script2 -e 's/three/3/' in
sed -e 's/one/1/' -e 's/two/2/' in
sed -ne '/f/p' in
echo "== r and w"
echo inserted > ins
sed '2r ins' in
sed '$r ins' in
sed 'r nosuch' in | wc -l
sed -n '/o/w ofile' in
cat ofile
sed -n -e '1w both' -e '3w both' in
cat both
sed -n '/e/w /dev/stdout' in
echo "== several files and stdin"
echo 'a 1' > f1
echo 'a 2' >> f1
echo 'b 1' > f2
echo 'b 2' >> f2
sed -n '$p' f1 f2
sed -n '1p;$p' f1 f2
sed -s -n '1p;$p' f1 f2
sed = f1 f2
sed -s = f1 f2
cat f2 | sed 's/^/>/' f1 - f1
echo "== no final newline"
echo -n last > nonl
sed '' nonl
echo '|'
sed p nonl
echo '|'
sed '$a more' nonl
echo '|'
sed -n '$p' nonl f1
echo '|'
sed -n '$p' f1 nonl
echo '|'
sed N nonl
echo '|'
sed 'N;N' f1 nonl
echo '|'
sed -n 'p;w nl' nonl
echo '|'
cat nl
echo '|'
echo "== -i"
cp in e1
cp f1 e2
sed -i 's/o/0/g;1d' e1 e2
cat e1 e2
sed -i.bak '$s/$/ end/' e2
cat e2
cat e2.bak
sed -i -n '1p' e1
cat e1
sed -i p nosuch
echo "status $?"
echo "== a long line"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8
do
    cat a a > b
    mv b a
done
echo >> a
wc -c a
sed 's/0/zero/g' a | wc -c
sed 's/[0-9]*/x/' a
sed -n 's/\(01\)\(.*\)\(89\)$/\3\2\1/p' a | wc -c
sed -E 's/(0123456789)+/x/' a
sed 'h;G;G;s/\n//g' a | wc -c
sed -n '/^\(0123456789\)*$/p' a | wc -c
echo "== errors"
sed
echo "status $?"
sed -x p in
echo "status $?"
sed -e
echo "status $?"
sed k in
echo "status $?"
sed 's/a/b' in
echo "status $?"
sed 's/a/b/q' in
echo "status $?"
sed 's/\(a/b/' in
echo "status $?"
sed 's/a/\1/' in
echo "status $?"
sed 's/[a/b/' in
echo "status $?"
sed 'y/abc/de/' in
echo "status $?"
sed '/x' in
echo "status $?"
sed 'b nowhere' in
echo "status $?"
sed '1,2}' in
echo "status $?"
sed '{p' in
echo "status $?"
sed 'p x' in
echo "status $?"
sed 'a' in
echo "status $?"
sed '1,2=' in | head -1
sed ':' in
echo "status $?"
sed 's/a\{3/b/' in
echo "status $?"
sed -E 's/a{2,1}/b/' in
echo "status $?"
sed 's/[[:foo:]]/b/' in
echo "status $?"
sed 's/x/y/0' in
echo "status $?"
sed -f nosuch in
echo "status $?"
sed p nosuch f1
echo "status $?"
sed -n p f1 nosuch
echo "status $?"
sed 's//x/' in
echo "status $?"
sed -n '/one/s//1/p' in
echo "status $?"
