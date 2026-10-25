# words: unquoted variables split, quoted ones keep their spaces, empty ones vanish
X="a  b   c"
for w in $X; do echo "[$w]"; done
echo "$X"
E=
echo one $E two
