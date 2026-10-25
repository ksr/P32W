# du: directories, files, -a, -s, sizes in KiB, links counted once
mkdir d d/e
echo 1 > d/small
: > d/empty
echo 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde > d/e/big
i=0
while test $i -lt 7
do
    cat d/e/big d/e/big > d/e/tmp; mv d/e/tmp d/e/big
    i=`expr $i + 1`
done
wc -c d/e/big
du d
echo "status $?"
echo "-- a file"
du d/small d/empty d/e/big
echo "-- -a"
du -a d | sort
echo "-- -s"
du -s d
du -s d/e d/small
echo "-- 8193 bytes: 9 blocks and an indirect block"
echo >> d/e/big
du d/e/big
echo "-- links are counted once"
ln d/e/big d/big2
du d
du -a d | sort
echo "-- a trailing /, ."
du d/e/
cd d/e; du; cd ../..
echo "-- errors"
du nosuch; echo "status $?"
du nosuch d/small; echo "status $?"
du -x d; echo "status $?"
