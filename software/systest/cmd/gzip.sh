# gzip, gunzip, zcat: options -c -d -f -k -n -N -q -t -v -0..-9, -, suffixes, errors
echo hello hello hello hello > f
for i in 1 2 3 4 5 6 7 8 9 10
do
    echo "line $i of a file that compresses well, line $i" >> r
done
cp r rc
echo "== compress and expand"
gzip f
echo "status $?"
ls
gzip -t f.gz
echo "status $?"
gunzip f.gz
echo "status $?"
ls
cat f
echo "== keep, replace, force"
gzip -k r
echo "status $?"
ls
gzip r
echo "status $?"
gzip -f r
echo "status $?"
ls
zcat r.gz > r2
cmp r2 rc && echo same
gzip -dc r.gz > r3
cmp r3 rc && echo same
gunzip -k r.gz
echo "status $?"
cmp r rc && echo same
rm r.gz r2 r3
echo "== the modes are kept"
chmod 600 r
gzip r
ls -l r.gz | grep -c '^-rw------- '
gunzip r.gz
ls -l r | grep -c '^-rw------- '
echo "== levels"
for n in 0 1 6 9
do
    gzip -c -$n r > r.gz
    gzip -t r.gz
    zcat r.gz > r2
    cmp r2 r && echo "level $n same"
done
gzip -9 -c r > r9.gz
gzip -0 -c r > r0.gz
ls -l r0.gz | grep -c ' 487 '
zcat r0.gz r9.gz > r2
cat r r > rr
cmp r2 rr && echo same
rm r.gz r0.gz r9.gz r2 rr
echo "== - is the standard input"
echo abc | gzip > s.gz
zcat s.gz
echo xyz | gzip - > t.gz
gzip -d - < t.gz
gzip -dc - < s.gz
cat s.gz t.gz | zcat
gunzip < t.gz
echo "== -v"
gzip -v f
gunzip -v f.gz
gzip -kv f
gunzip -fkv f.gz
gzip -cv f > f2.gz
gzip -tv f.gz f2.gz
rm f.gz f2.gz
echo "== -N restores the stored name"
echo data > orig
gzip orig
mv orig.gz other.gz
gunzip -N other.gz
echo "status $?"
ls
gzip orig
mv orig.gz other.gz
gunzip other.gz
ls
echo "== -n stores no name"
gzip -n other
mv other.gz third.gz
gunzip -N third.gz
ls
echo "== -q silences warnings, not errors"
echo x > w.gz
gzip w.gz
echo "status $?"
gzip -q w.gz
echo "status $?"
gunzip -q w.gz
echo "status $?"
gunzip -q nosuch.gz
echo "status $?"
echo "== trailing garbage"
gzip -c f > g.gz
echo junk >> g.gz
zcat g.gz
echo "status $?"
zcat -q g.gz
echo "status $?"
echo "== errors"
gunzip f
echo "status $?"
mkdir dir.gz
gunzip dir.gz
echo "status $?"
gzip -dc f
echo "status $?"
gzip -t w.gz
echo "status $?"
cp g.gz h.gz
gzip -d h.gz h2.gz
echo "status $?"
ls h*
gzip -x f
echo "status $?"
