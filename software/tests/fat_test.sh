#!/bin/sh
# fat_test.sh -- the FAT tools (dosdir, doscp, dosrm, dosmkdir, dosformat;
# D44) against the Mac's own FAT code, as archive_test.sh does for the
# archivers. A stick image the Mac formats and fills (hdiutil: FAT32 in an
# MBR partition, long names, a name in UTF-8, a folder tree) is read and
# written by P32W Unix in p32emu (the image as its USB drive); then the Mac
# checks what P32W left: fsck_msdos, and the files through a read-only
# mount. Then sticks dosformat made (FAT32, FAT16), and one that fills up.
# The images are attached with -nobrowse (not shown in Finder) and detached
# at once. Prints PASS / FAIL lines; macOS only.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32fat.$$
P=../tools/p32fs.py
mkdir -p $T/mnt $T/tree
trap 'hdiutil detach $T/mnt >/dev/null 2>&1; [ -z "$KEEP" ] && rm -rf $T' EXIT
st=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1: $2"; st=1; }

# raw image -> its partition's device (attached, not mounted): $DEV; detach with $WHOLE
attach() {
    hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "$1" > $T/att 2>/dev/null
    WHOLE=$(head -1 $T/att | awk '{print $1}')
    DEV=$(awk '/s1[ \t]/{print $1}' $T/att)
}
# fsck_msdos -n on image $1: PASS or FAIL line $2
macfsck() {
    attach "$1"
    if [ -z "$DEV" ]; then fail "$2" "the Mac found no FAT partition"; hdiutil detach $WHOLE >/dev/null 2>&1; return 1; fi
    fsck_msdos -n $DEV > $T/fsck 2>&1; r=$?
    hdiutil detach $WHOLE >/dev/null 2>&1
    if [ $r -eq 0 ] && ! grep -qi "fix\|lost\|incorrect\|invalid\|error" $T/fsck; then pass "$2"
    else fail "$2" "$(grep -v '^\*\*' $T/fsck | head -3 | tr '\n' ' ')"; fi
}
mountro() { hdiutil attach -readonly -nobrowse -mountpoint $T/mnt -imagekey diskimage-class=CRawDiskImage "$1" >/dev/null 2>&1; }
unmount() { hdiutil detach $T/mnt >/dev/null 2>&1; }
run() {     # run IMAGE: P32W Unix with IMAGE as its USB drive, $T/in as the terminal
    cp build/test.img $T/os.img
    ../emulator/p32emu -w -R 1791936000 -l 20000000000 -d $T/os.img -U "$1" $T/monitor.bin < $T/in > $T/log 2>&1
    nrun=$((nrun + 1)); cp $T/log $T/log.$nrun        # (kept with KEEP=1)
}
nrun=0
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin

# 1. a stick the Mac made, read and written on P32W
( cd $T/tree && mkdir -p "Photos 2026/sub" &&
  echo "hello from the Mac" > "A long name, from the Mac.txt" && echo short > README.TXT && echo low > lower.c &&
  printf 'caf\303\251\n' > "caf$(printf '\303\251').txt" &&
  python3 -c "import sys; sys.stdout.buffer.write(bytes((i * 7 + i // 256) & 255 for i in range(100000)))" > "Photos 2026/sub/IMG_0001.JPG" )
hdiutil create -size 64m -fs "MS-DOS FAT32" -layout MBRSPUD -volname MACSTICK $T/mac.dmg >/dev/null 2>&1 &&
hdiutil attach $T/mac.dmg -nobrowse -mountpoint $T/mnt >/dev/null 2>&1 &&
cp -R $T/tree/ $T/mnt/ && find $T/mnt -name '._*' -delete && unmount &&   # (macOS 27 tags every new file, which a FAT stick keeps as a ._ AppleDouble file)
hdiutil convert $T/mac.dmg -format UDTO -o $T/mac >/dev/null 2>&1 || { echo "FAIL the Mac could not make a FAT32 stick image"; exit 1; }
cat > $T/in <<'IN'
b
ken
p32
dosdir
mkdir /tmp/m
doscp -r :: /tmp/m
ls /tmp/m | grep caf | od -c
cat /tmp/m/caf*
doscp -r /usr/include/sys ::/fromp32
doscp /bin/ls ::/fromp32/LS.BIN
dosmkdir "::Photos 2026/made on P32W"
dosrm ::lower.c
dosrm -r "::Photos 2026/sub"
su
root
halt
IN
run $T/mac.cdr
grep -q "FAT32 volume MACSTICK" $T/log && grep -q "A long name, from the Mac.txt" $T/log &&
    pass "dosdir: a FAT32 stick the Mac made (MBR, long names)" || fail "dosdir: the Mac's stick" "$(grep dos $T/log | head -3 | tr '\n' ' ')"
bad=""
for f in README.TXT lower.c "Photos 2026/sub/IMG_0001.JPG"; do
    python3 $P cat $T/os.img "/tmp/m/$f" > $T/got 2>/dev/null; cmp -s $T/got "$T/tree/$f" || bad="$bad $f"
done
sn=$(tr -d '\r' < $T/log | sed -n 's/.*the name is too long for P32W: copied as //p')     # (the short name the Mac chose)
python3 $P cat $T/os.img "/tmp/m/$sn" > $T/got 2>/dev/null; cmp -s $T/got "$T/tree/A long name, from the Mac.txt" || bad="$bad (the long name as its short name, '$sn')"
tr -d '\r' < $T/log | grep -q "c   a   f 303 251   .   t   x   t" || bad="$bad (the UTF-8 name)"     # (the terminal is 7-bit: the name is checked as od shows it)
tr -d '\r' < $T/log | grep -q "^caf$(printf '\303\251')\$" || bad="$bad (café.txt's contents)"
[ -z "$bad" ] && pass "doscp: the Mac's files copied to P32W (a UTF-8 name; one over 28 characters as its short name)" || fail "doscp: the Mac's files to P32W" "$bad"
macfsck $T/mac.cdr "fsck_msdos: the Mac's stick after P32W wrote to it, clean"
if mountro $T/mac.cdr; then
    bad=""
    diff -r -x LS.BIN include/sys $T/mnt/fromp32 > $T/diff || bad="$bad fromp32: $(head -2 $T/diff)"
    cmp -s build/bin/ls $T/mnt/fromp32/LS.BIN || bad="$bad LS.BIN"
    [ -d "$T/mnt/Photos 2026/made on P32W" ] || bad="$bad (the new directory)"
    [ -e $T/mnt/lower.c ] && bad="$bad (lower.c not removed)"
    [ -e "$T/mnt/Photos 2026/sub" ] && bad="$bad (sub not removed)"
    cmp -s $T/mnt/README.TXT $T/tree/README.TXT || bad="$bad README.TXT"
    unmount
    [ -z "$bad" ] && pass "doscp, dosmkdir, dosrm: the Mac reads what P32W wrote (a tree, a binary, a new directory, removals)" || fail "the Mac reading P32W's writes" "$bad"
else fail "the Mac reading P32W's writes" "it would not mount"; fi

# 2. sticks dosformat made: FAT32 (600 MB: 4 KiB clusters), FAT16 (40 MB)
for spec in "600 FAT32 dosformat+-v+P32STICK" "40 FAT16 dosformat"; do
    set -- $spec
    mb=$1; fat=$2; cmd=$(echo $3 | tr '+' ' ')
    python3 -c "open('$T/blank.img', 'wb').truncate($mb * 1024 * 1024)"
    printf '%s\n' b ken p32 "$cmd" 'doscp -r /usr/include ::' 'dosdir' su root halt > $T/in
    run $T/blank.img
    macfsck $T/blank.img "dosformat: a $mb MB stick as $fat, clean to fsck_msdos" || continue
    if mountro $T/blank.img; then
        if diff -r -x sqldb.h include $T/mnt/include > /dev/null && cmp -s sql/sqldb.h $T/mnt/include/sqldb.h && diskutil info $T/mnt | grep -q "MS-DOS $fat"; then
            pass "dosformat: the Mac mounts the $mb MB stick as $fat and reads P32W's files"
        else fail "dosformat: the $mb MB stick on the Mac" "$(diff -rq -x sqldb.h include $T/mnt/include | head -2 | tr '\n' ' ')"; fi
        unmount
    else fail "dosformat: the $mb MB stick" "the Mac would not mount it"; fi
done

# 3. a stick that fills up: the copy that does not fit is undone, and nothing is lost
python3 -c "open('$T/blank.img', 'wb').truncate(4 * 1024 * 1024)"     # (two micropythons fit, three do not)
printf '%s\n' b ken p32 dosformat 'doscp /usr/bin/micropython ::' 'doscp /usr/bin/micropython ::second' 'doscp /usr/bin/micropython ::third' 'dosdir' 'doscp /etc/motd ::' su root halt > $T/in
run $T/blank.img
grep -q "third: the drive is full" $T/log && pass "doscp: a full drive is reported" || fail "doscp: a full drive" "$(grep -i 'doscp' $T/log | tail -2 | tr '\n' ' ')"
macfsck $T/blank.img "doscp: the full drive is clean to fsck_msdos (the copy that failed left nothing)"
exit $st
