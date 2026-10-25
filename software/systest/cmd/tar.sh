# tar: c t x, v and z, names, hard links, absolute names, the archive itself, errors
mkdir d d/sub
echo one > d/a
echo two two > d/sub/b
for i in 1 2 3 4 5 6 7 8 9 10
do
    echo "line $i of a file that compresses well, line $i" >> d/lines
done
ln d/a d/a2
chmod 640 d/sub/b
echo "== create and list"
tar cf d.tar d
echo "status $?"
tar tf d.tar
tar -tf d.tar d/sub
echo "== a long listing (mode owner/group size date name)"
tar tvf d.tar | grep -c '^-rw-r--r-- [a-z0-9]*/[a-z0-9]*  *4 20[0-9][0-9]-[01][0-9]-[0-3][0-9] [0-2][0-9]:[0-5][0-9] d/a$'
tar tvf d.tar | grep -c '^hrw-r--r-- .* 0 .* d/a2 link to d/a$'
tar tvf d.tar | grep -c '^-rw-r----- .*  *8 .* d/sub/b$'
tar tvf d.tar | grep -c '^drwx'
echo "== extract"
mkdir x
cd x
tar xf ../d.tar
echo "status $?"
cmp d/a ../d/a && cmp d/sub/b ../d/sub/b && cmp d/lines ../d/lines && echo same
ls -l d/a2 | grep -c '^-rw-r--r--  *2 '
ls -l d/sub/b | grep -c '^-rw-r----- '
cd ..
echo "== extract some, verbose"
mkdir y
cd y
tar xvf ../d.tar d/sub
echo "status $?"
ls d
ls d/sub
tar xvf ../d.tar nosuch
echo "status $?"
cd ..
echo "== create verbose, from stdout to stdin"
tar cvf - d/sub > s.tar
tar tf - < s.tar
cat s.tar | tar t
echo "== z"
tar czf d.tgz d
echo "status $?"
gzip -t d.tgz
echo "status $?"
tar tzf d.tgz | grep -c /
mkdir z
cd z
tar xzf ../d.tgz d/lines
cmp d/lines ../d/lines && echo same
cd ..
echo "== absolute names lose the leading /"
tar cf abs.tar /usr/examples/hello.c /usr/examples/README
echo "status $?"
tar tf abs.tar
tar xf abs.tar
echo "status $?"
cmp usr/examples/hello.c /usr/examples/hello.c && echo same
echo "== the archive itself is left out"
mkdir s
echo s > s/f
cd s
tar cf self.tar .
echo "status $?"
tar tf self.tar
cd ..
echo "== .. is not extracted"
cd x
tar cf ../up.tar ../d/a
cd ..
tar tf up.tar
mkdir u
cd u
tar xf ../up.tar
echo "status $?"
cd ..
echo "== errors"
tar cf e.tar d/a nosuch
echo "status $?"
tar tf e.tar
tar xf nosuch.tar
echo "status $?"
echo x > notar
tar tf notar
echo "status $?"
tar
echo "status $?"
tar vf d.tar
echo "status $?"
tar tq d.tar
echo "status $?"
tar c
echo "status $?"
tar tf
echo "status $?"
tar tf d.tar d/a nosuch d/sub
echo "status $?"
