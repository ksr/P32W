# cut: -b -c -f lists, -d -s, files, long lines, errors
cat > f <<END
name	uid	shell
ken	100	/bin/sh
root	0
no tabs here
	trailing	
END
cat > p <<'END'
root:x:0:0:Charlie &:/root:/bin/sh
ken:x:100:10::/home/ken:/bin/sh
a::c
nocolon
END
echo "== -c and -b lists"
echo abcdefghij | cut -c 3
echo abcdefghij | cut -c 2-4
echo abcdefghij | cut -c -3
echo abcdefghij | cut -c 8-
echo abcdefghij | cut -c 1,3,5-6,9-
echo abcdefghij | cut -b 9-,1-2
echo abcdefghij | cut -c '7 2 4-5'
echo abcdefghij | cut -c 2-5,3-4,4-8
echo abcdefghij | cut -n -b 20-
echo abcdefghij | cut -c1-3,5
echo abc | cut -c 1-100
echo "status $?"
echo "== fields, tab by default"
cut -f 1 f
cut -f 2,3 f
cut -f 3- f
cut -s -f 1,3 f
echo "== -d"
cut -d: -f 1,7 p
cut -d : -f 6- p
cut -d: -f 2,3 p
cut -d: -s -f 1 p
cut -d ' ' -f 2 p
cut -d: -f 1-1000 p
echo "== files, -, no last newline"
echo -n last > nl
cut -c 1-2 nl p - nl < f
echo "status $?"
cut -d: -f 1 nosuch p
echo "status $?"
echo "== a long line"
echo -n 0123456789 > a
for i in 1 2 3 4 5 6 7 8 9 10
do
    cat a a > b
    mv b a
done
cut -c 10235- a
cut -c 3-10238 a | wc -c
tr 5 , < a > b
cut -d , -f 1024- b
cut -d , -f 2-1024 b | wc -c
echo "== errors"
cut
echo "status $?"
cut f
echo "status $?"
cut -c 1 -f 2 f
echo "status $?"
cut -c 1 -d : f
echo "status $?"
cut -b 1 -s f
echo "status $?"
cut -f 1 -d :: f
echo "status $?"
cut -f 1 -d '' f
echo "status $?"
cut -c 0 f
echo "status $?"
cut -c 0-2 f
echo "status $?"
cut -c 5-2 f
echo "status $?"
cut -c a f
echo "status $?"
cut -c 1, f
echo "status $?"
cut -c - f
echo "status $?"
cut -c 1--2 f
echo "status $?"
cut -c
echo "status $?"
cut -q -c 1 f
echo "status $?"
