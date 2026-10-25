# the newer shell: functions, case, NAME=v cmd, ( ) and { }, here-documents,
# set / shift / read, eval, umask, . file, #! scripts, set -e
# functions, return, $1 $#
greet() {
    echo "hello $1 ($# args)"
    return 3
}
greet world a b
echo "status $?"
# case
for x in apple b17 zzz ""; do
    case $x in
    a*|b*) echo "$x: a or b" ;;
    [0-9]*) echo "$x: digit" ;;
    "") echo "empty" ;;
    *) echo "$x: other" ;;
    esac
done
case yes in y*) echo one-line case ;; esac
# NAME=v cmd only for the command
FOO=bar sh -c 'echo inside $FOO'
echo "outside [$FOO]"
# subshells and groups
(cd /; pwd)
pwd | grep -c tmp
(echo a; echo b) | wc -l
{ echo g1; echo g2; } > grp.out
cat grp.out
# here-documents
N=42
cat <<END
value $N
  kept
END
cat <<'END'
no $N here
END
# set, shift, read
set -- p q r
echo "$# $1 $2 $3"
shift
echo "$# $1"
echo "line one two three" > in.txt
read a b < in.txt
read c d < in.txt
echo "a=$a b=$b"
x=`echo a b c`
echo "x=$x"
# eval, umask, . file
eval 'echo evaluated $N'
umask 027
umask
echo 'SOURCED=yes' > inc.sh
. ./inc.sh
echo "sourced $SOURCED"
# #! scripts and plain scripts by name
echo '#!/bin/sh' > s1
echo 'echo from s1 $1' >> s1
chmod 755 s1
./s1 arg
echo 'echo plain script' > s2
chmod 755 s2
./s2
# set -e
sh -c 'set -e; false; echo not-reached'
echo "set -e status $?"
sh -c 'set -e; if false; then :; fi; false || true; echo reached'
