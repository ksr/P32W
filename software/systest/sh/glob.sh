# glob.sh: wildcards over many names: a pattern matching 100 of 600 files,
# in a for list and as arguments; one matching all 600 is an error ("too
# many names", status 2), not a list silently cut short
mkdir g
cd g
for a in 0 1 2 3 4 5
do
    for b in 0 1 2 3 4 5 6 7 8 9
    do
        for c in 0 1 2 3 4 5 6 7 8 9
        do
            > f$a$b$c
        done
    done
done
n=0
for f in f3*
do
    n=`expr $n + 1`
done
echo "f3*: $n names, the last $f"
echo f5* | wc -w
ls f0?? | head -3
for f in *
do
    echo "never: $f"
done
echo "for status $?"
echo *
echo "echo status $?"
ls f* | wc -l
cd ..
rm -r g
echo done
