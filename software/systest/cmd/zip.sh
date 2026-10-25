# zip: new archives and updates, -r -q -0..-9, entry names, the archive name, errors
# (the entries are checked with unzip -t, which names them and checks their CRCs)
mkdir d d/sub
echo one > d/a
echo two two > d/sub/b
for i in 1 2 3 4 5 6 7 8 9 10
do
    echo "line $i of a file that compresses well, line $i" >> d/lines
done
echo "== a new archive (.zip added)"
zip a d/a d/sub/b
echo "status $?"
ls
unzip -t a
ls -l a.zip | grep -c '^-rw-r--r-- '
echo "== an update replaces entries, keeps the others"
echo three > d/a
zip -q a d/a d/lines
echo "status $?"
unzip -tq a
mkdir x
unzip -q -d x a
cmp x/d/a d/a && cmp x/d/sub/b d/sub/b && cmp x/d/lines d/lines && echo same
echo "== the archive's mode is kept"
chmod 600 a.zip
zip -q a d/sub/b
ls -l a.zip | grep -c '^-rw------- '
echo "== a directory without and with -r"
zip dd d
echo "status $?"
unzip -t dd
zip -rq rr d
unzip -t rr
echo "== a directory as the archive name: .zip added"
zip -rq d d
echo "status $?"
ls
unzip -t d
echo "== leading / and ./ are left out, of directories too"
zip -q ex /usr/examples/README /usr/examples/hello.c
unzip -t ex
zip -rq ex2 /usr/examples
unzip -l ex2 | grep -c ' usr/examples/$'
unzip -t ex2 usr/examples/hello.c
zip -rq dot ./d/sub
unzip -t dot
mkdir y
cd y
unzip -q ../ex
echo "status $?"
cmp usr/examples/hello.c /usr/examples/hello.c && echo same
cd ..
echo "== the archive itself is left out"
mkdir s
echo s > s/f
cd s
zip -r self .
zip -r self.zip .
unzip -t self
cd ..
echo "== -0 stores, -9 compresses"
zip -0 st d/lines
unzip -l st | grep -c '^      462  20'
zip -9q best d/lines
unzip -tq best
echo "== an empty file is stored"
touch empty
zip e empty
unzip -t e
echo "== errors"
zip n /dev/null d/a
echo "status $?"
unzip -t n
zip n2 nosuch
echo "status $?"
echo x > nz.zip
zip nz d/a
echo "status $?"
zip d/a d/sub/b
echo "status $?"
zip a
echo "status $?"
zip
echo "status $?"
zip -x a d/a
echo "status $?"
ls
