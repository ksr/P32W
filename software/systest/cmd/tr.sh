# tr: translate, ranges, escapes, classes, repeats, -c -d -s, errors
cat > f <<'END'
Hello, World 42!
the  quick   brown fox
END
echo "== translate a range; string2 padded with its last character"
tr a-z A-Z < f
echo "status $?"
echo abcdef | tr a-f xy
echo abcdef | tr abc 123456
echo "== classes"
tr '[:lower:]' '[:upper:]' < f
tr '[:upper:][:lower:]' '[:lower:][:upper:]' < f
tr '[:digit:]' '#' < f
tr '[:punct:]' '.' < f
echo "== escapes: newline, tab, octal, backslash"
echo 'a b c' | tr ' ' '\n'
echo 'a:b' | tr : '\t' | od -c
echo 'a\b' | tr '\\' /
echo abc | tr '\141\142' '\101\102'
echo abc | tr 'a' '\0' | od -c
echo "== repeats [c*n] and [c*]"
echo abcdefgh | tr a-h '[x*3]y'
echo abcdefgh | tr a-h 'A[-*]H'
echo abcdefgh | tr a-h 'a[**]'
echo abcdefgh | tr a-h '[x*010]'
echo "== a - at either end, [=c=], a lone ["
echo 'a-b-c' | tr -- '-a' '_A'
echo 'a-b-c' | tr 'c-' 'C+'
echo 'a=b' | tr '[=a=]' z
echo 'x[y]' | tr '[]' '()'
echo "== -d, -dc"
tr -d aeiou < f
tr -d '[:space:]' < f
echo
tr -dc '[:digit:]\n' < f
echo "== -s, -cs, -ds"
tr -s ' ' < f
echo 'aaabbbccc  dd' | tr -s 'a-c '
tr -cs '[:alpha:]' '[\n*]' < f
echo 'aabbccaa' | tr -s ab xy
echo 'aaa bbb aaa' | tr -ds b a
echo "== -c translates the complement, in order"
echo 'ab1-2' | tr -c '[:digit:]\n' _
echo 'ab1-2' | tr -C 'a-z\n' 'X'
echo "== the last of a repeated mapping counts; NUL passes through"
echo aaa | tr aa xy
echo abc | tr b '\0' | tr '\0' Z
echo "== a long line, no last newline"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8 9 10
do
    cat a a > b
    mv b a
done
tr 0-9 a-j < a | wc -c
tr 0-9 a-j < a | tr -d a-i | wc -c
tr -s 0-9 < a | wc -c
echo "== errors"
tr
echo "status $?"
tr a
echo "status $?"
tr a b c
echo "status $?"
tr -d a b
echo "status $?"
tr -ds a
echo "status $?"
tr -x a b
echo "status $?"
echo abc | tr z-a x
echo "status $?"
echo abc | tr '[:nosuch:]' x
echo "status $?"
echo abc | tr '[a*]' x
echo "status $?"
echo abc | tr abc '[x*][y*]'
echo "status $?"
echo abc | tr abc ''
echo "status $?"
