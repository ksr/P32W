# join: pairs, groups, -a -v -e -o -t -1 -2 -j, blanks, errors
cat > names <<'END'
1 ann
2 bob
2 bobby
4 dan
5 eve
END
cat > ages <<'END'
1 30 f
2 40 m
3 50 x
5 60 f
5 61 g
6 70
END
echo "== the default: field 1, the rest of each line"
join names ages
echo "status $?"
echo "== -a, -v"
join -a 1 names ages
join -a2 names ages
join -a 1 -a 2 names ages
join -v 1 names ages
join -v2 names ages
join -v 1 -v 2 names ages
echo "== -o, -e"
join -o 1.2,2.2 names ages
join -o '2.3 1.2 0' names ages
join -a 1 -a 2 -o 0,1.2,2.2 -e NONE names ages
join -a 1 -o 1.1,2.3,2.9 -e - names ages
join -o 2.1,1.1 -a2 names ages
echo "== -1 -2 -j: other join fields"
cat > byname <<'END'
ann red
bob blue
eve green
END
join -1 2 -2 1 names byname
join -2 1 -1 2 -o 1.1,0,2.2 names byname
cat > t1 <<'END'
x 1
y 2
END
cat > t2 <<'END'
p 1
q 2
END
join -j 2 t1 t2
echo "== blanks: leading blanks, tabs and runs of them"
cat > s1 <<'END'
   k1	 one   two
k2 three
END
cat > s2 <<'END'
k1		uno
	k2 dos  tres
END
join s1 s2
echo "== -t: every separator counts, empty fields"
cat > c1 <<'END'
:3:3
a:1:
b::2
END
cat > c2 <<'END'
:empty
a:x
b:y:z
END
join -t: c1 c2
join -t : -e '?' c1 c2
join -t: -o 1.2,1.3,2.2 -e E c1 c2
join -t' ' names ages
echo "== - for the standard input; no last newline"
join - ages < names
echo -n '5 last' > nl
join nl ages
join ages nl
echo "== groups on both sides: every pair"
cat > g1 <<'END'
k a
k b
m c
END
cat > g2 <<'END'
k 1
k 2
k 3
m 4
END
join g1 g2
join -v1 g1 g2
echo "status $?"
echo "== a long line"
echo -n 0123456789 > l
for i in 1 2 3 4 5 6 7 8 9 10
do
    cat l l > m
    mv m l
done
echo -n 'key ' > l1
cat l >> l1
echo >> l1
echo 'key end' > l2
join l1 l2 | wc -c
join -o 2.2 l1 l2
echo "== errors"
join names
echo "status $?"
join -a 3 names ages
echo "status $?"
join -v x names ages
echo "status $?"
join -1 0 names ages
echo "status $?"
join -2 x names ages
echo "status $?"
join -o 3.1 names ages
echo "status $?"
join -o 1.0 names ages
echo "status $?"
join -o 1.1, names ages
echo "status $?"
join -t ab names ages
echo "status $?"
join -k names ages
echo "status $?"
join -e
echo "status $?"
join - - < names
echo "status $?"
join names nosuch
echo "status $?"
