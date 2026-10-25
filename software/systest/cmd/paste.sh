# paste: merging, -d lists, -s, - for the standard input, errors
cat > n <<'END'
1
2
3
END
cat > l <<'END'
a
b
END
: > e
echo -n x > nl
echo "== side by side; a shorter file gives empty parts"
paste n l
echo "status $?"
paste l n
paste n e l
paste n | od -c
echo "== -d: a list used in turn, escapes"
paste -d : n l
paste -d ':,' n l n
paste -d':,' n l n n
paste -d '\t-' n l n
paste -d '\n' n l
paste -d '\\' n l
paste -d '\0' n l n
paste -d '' n l
paste -d 'x\0y' n n n n
echo "== -s"
paste -s n
paste -s n l e nl
paste -s -d , n l
paste -s -d ',;' n
paste -sd '\n' l
paste -d - -s n
echo "== - for the standard input, more than once; no file"
paste - - < n
paste - l - < n
paste -s - n < l
paste < l
echo "== no last newline"
paste nl nl
paste nl n
echo "== a long line"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8 9 10
do
    cat a a > b
    mv b a
done
paste a a | wc -c
paste -s a n a | wc -c
echo "== errors"
paste -x n
echo "status $?"
paste -d
echo "status $?"
paste n nosuch l
echo "status $?"
paste -s n nosuch l
echo "status $?"
