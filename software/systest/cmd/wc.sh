# wc: lines, words, bytes; -l -w -c in any combination; several files and a total
echo one two three > f
echo '  four	five  ' >> f
echo >> f
echo -n six >> f
echo -n > empty
echo "== all counts"
wc f
echo "status $?"
wc < f
echo "== each option, and combinations"
wc -l f
wc -w f
wc -c f
wc -lw f
wc -wc f
wc -l -c f
wc -cl f
echo "== several files and a total"
wc f empty f
wc -l f empty
echo "== - is the standard input"
echo a b | wc -w f -
echo "== errors"
wc -x f
echo "status $?"
wc nosuch f
echo "status $?"
