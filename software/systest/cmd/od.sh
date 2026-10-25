# od: hex bytes (default, -x) and characters (-c), offsets in octal, several files
echo hi > f
echo "== echo hi | od -c (the manual's example)"
echo hi | od -c
echo "== default and -x"
od f
od -x f
echo "== 16 bytes a line; the last line is the length"
echo abcdefghijklmnopqrstuvwxyz > g
od g
od -c g
echo "== tab, other bytes as octal"
echo 'a	b' > t
od -c t
echo -n '~' | od -c
echo "== control bytes, written into this script as they are"
echo -n '€ÿ' | od -c
echo -n '€ÿ' | od
echo "== an empty input"
od < /dev/null
echo "== several files are one stream; - is the standard input"
echo -n 0123456789 > d
od -c d d
echo xyz | od -c d -
echo "== errors"
od -o f
echo "status $?"
od nosuch
echo "status $?"
