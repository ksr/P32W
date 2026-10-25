# xargs: words and quoting, -n -L -I -s -x -t -r -0 -E, the P32W limits,
# exit statuses
echo 'echo $#: $*' > cnt
cat > fail <<'END'
echo "run $1"
test $1 = bad && exit 1
test $1 = stop && exit 255
test $1 = kill && kill -9 $$
exit 0
END
cat > words <<'END'
one two  three
	four

five
END
echo "== echo by default; blanks, tabs, empty lines"
xargs < words
echo "status $?"
xargs sh cnt < words
echo "== quotes and backslashes"
cat > q <<'END'
'a b' "c  d" e\ f
'it''s' "it's" 'say "hi"' x\\y \'
'' "" g\
h
mixed"q 'q"'u "x'
END
xargs sh cnt < q
xargs -n 1 echo < q
echo "== -n"
xargs -n 2 < words
xargs -n 2 sh cnt < words
xargs -n 10 < words
echo "== -L: lines; a trailing blank goes on to the next line"
cat > lines <<'END'
a b
c

d e f 
g
h
END
xargs -L 1 sh cnt < lines
xargs -L 2 sh cnt < lines
echo "== -I: a whole line, every occurrence replaced"
cat > ins <<'END'
  first line  
second 'quoted part'
x

END
xargs -I X echo '<X>' X-X < ins
xargs -I {} sh cnt pre {} post < ins
echo echo | xargs -I C C replaced command
echo "== -t: the command to the standard error"
xargs -t -n 3 echo < words 2> err
cat err
echo "== -r and empty input"
xargs < /dev/null
echo "status $?"
xargs -r echo nothing < /dev/null
echo "status $?"
echo '   ' | xargs sh cnt
echo "== -0: NUL-separated, quotes ordinary"
tr '\n' '\0' < q | xargs -0 sh cnt
tr '\n' '\0' < words | xargs -0 -n 1 echo '|'
echo "== -E: a logical end of the input"
echo 'a b END c d' | xargs -E END
echo 'a b END c d' | xargs -E c
echo "== -s: bytes of each command line (strings and NULs)"
echo a b c d e f | xargs -s 10
echo a b c d e f | xargs -s 11 sh cnt
echo aaaa bb c | xargs -s 12 echo
echo a b c d e f | xargs -n 3 -s 9
echo "status $?"
echo "== -x: stop when -n words do not fit"
echo a b c d | xargs -x -n 2 -s 9
echo "status $?"
echo a b c d | xargs -x -n 2 -s 8
echo "status $?"
echo "== the command's input is /dev/null"
echo x | xargs sh -c 'cat; echo done'
echo "== at most 249 arguments: 512 one-character words"
echo x > w
for i in 1 2 3 4 5 6 7 8 9
do
    cat w w > w2
    mv w2 w
done
wc -l < w
xargs < w | wc
xargs sh cnt < w > c
cut -d: -f1 c
echo "== at most 4096 bytes: 256 words of 20 bytes"
echo abcdefghijklmnopqrs > w
for i in 1 2 3 4 5 6 7 8
do
    cat w w > w2
    mv w2 w
done
xargs < w | wc -l
xargs < w | wc -w
xargs -s 2000 < w | wc -l
xargs -s 100000 < w | wc -l
echo "== exit statuses: 123 (and go on), 124, 125, 126, 127"
echo good bad good | xargs -n 1 sh fail
echo "status $?"
echo good stop good | xargs -n 1 sh fail
echo "status $?"
echo good kill good | xargs -n 1 sh fail
echo "status $?"
echo x | xargs nosuchcommand
echo "status $?"
echo x | xargs /bin/nosuch
echo "status $?"
echo x | xargs /tmp
echo "status $?"
echo 'echo hi' > noexec
echo x | xargs ./noexec
echo "status $?"
echo x | xargs false
echo "status $?"
echo x | xargs true
echo "status $?"
echo "== errors"
echo "a 'b" | xargs
echo "status $?"
echo 'a "b' | xargs
echo "status $?"
echo x | xargs -n 0
echo "status $?"
echo x | xargs -n x
echo "status $?"
echo x | xargs -L
echo "status $?"
echo x | xargs -s 0
echo "status $?"
echo x | xargs -q
echo "status $?"
echo x | xargs -I '' echo
echo "status $?"
echo abcdefgh | xargs -s 10
echo "status $?"
echo x | xargs -s 4 echo
echo "status $?"
