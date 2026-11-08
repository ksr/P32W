#!/bin/sh
# archive_test.sh -- the archivers against the Mac's (D25): archives made on
# the host (tar, tar -z, gzip, Info-ZIP zip, ditto -- what Finder uses) are
# unpacked by P32W Unix (in p32emu), which packs the tree again with its own
# tar / gzip / zip; the host unpacks those and compares them with the
# original (contents, modes, times, links). Prints PASS / FAIL lines.
# Elsewhere (Linux, a codespace: D96) the host's are GNU tar, gzip and Info-ZIP; ditto
# is the Mac's own, and the two checks that need it print SKIP lines there.
cd "$(dirname "$0")/.."
T=${TMPDIR:-/tmp}/p32arch.$$
P=../tools/p32fs.py
mkdir -p $T/tree
DITTO=; command -v ditto >/dev/null 2>&1 && DITTO=ditto.zip
PAX=; tar --version 2>/dev/null | grep -q GNU && PAX=--format=pax     # (GNU tar makes its own format unless asked; its .tgz stays so)
trap "[ -z \"$KEEP\" ] && rm -rf $T" EXIT
# the tree: nested directories, a path over 100 characters, a hard link, binary data, modes
K="$(cd ../kernel && pwd)"
D=proj/src/deep/er/and/deeper/still/going/on/and/on/to/make/a/long/path
( cd $T/tree && mkdir -p $D && echo "hello from the host" > proj/README &&
  printf 'int main(){return 0;}\n' > proj/src/main.c && chmod 600 proj/src/main.c &&
  python3 -c "import sys; sys.stdout.buffer.write(bytes((i * 7 + i // 256) & 255 for i in range(5000)))" > proj/src/blob.bin &&
  cat "$K"/*.c > proj/src/kernel.txt &&
  ln proj/README proj/README.link && echo deep > $D/file_with_a_long_name.txt &&
  touch -t 202601020304.05 proj/README proj/src/main.c proj/src/kernel.txt && touch -t 202512312359.59 proj/src/blob.bin &&
  COPYFILE_DISABLE=1 tar --format ustar -cf $T/ustar.tar proj && COPYFILE_DISABLE=1 tar $PAX -cf $T/pax.tar proj &&
  COPYFILE_DISABLE=1 tar -czf $T/pax.tgz proj && cp proj/src/kernel.txt $T/k.txt && gzip -9 -k $T/k.txt &&
  zip -qr $T/info.zip proj && { [ -z "$DITTO" ] || ditto -c -k --keepParent proj $T/ditto.zip; } )
python3 ../assembler/p32asm.py monitor/monitor.asm -o $T/monitor.bin
cp build/test.img $T/os.img
for f in ustar.tar pax.tar pax.tgz k.txt.gz info.zip $DITTO; do python3 $P put $T/os.img $T/$f /tmp/$f; done
cat > $T/in <<'IN'
b
root
root
mkdir /tmp/u /tmp/p /tmp/g /tmp/z /tmp/d
cd /tmp/u
tar xf /tmp/ustar.tar
tar cf /tmp/back-ustar.tar proj
cd /tmp/p
tar xf /tmp/pax.tar
tar cf /tmp/back-pax.tar proj
cd /tmp/g
tar xzf /tmp/pax.tgz
tar czf /tmp/back-tgz.tgz proj
gunzip /tmp/k.txt.gz
cd /tmp/z
unzip -q /tmp/info.zip
zip -rq /tmp/back-zip.zip proj
cd /tmp/d
unzip -q /tmp/ditto.zip
tar cf /tmp/back-ditto.tar proj
cd /tmp
unzip -t /tmp/back-zip.zip
halt
IN
../emulator/p32emu -w -R 1791936000 -l 20000000000 -d $T/os.img $T/monitor.bin < $T/in > $T/log 2>&1
st=0
# a file's time and mode, and its inode (python: stat's options differ on the Mac and Linux)
tm() { python3 -c 'import os, sys; s = os.stat(sys.argv[1]); print(int(s.st_mtime), oct(s.st_mode & 0o7777))' "$1"; }
ino() { python3 -c 'import os, sys; print(os.stat(sys.argv[1]).st_ino)' "$1"; }

check() {   # check NAME DESCRIPTION UNPACK-COMMAND (run in an empty directory, archive in $T/NAME) [notimes]
    python3 $P cat $T/os.img /tmp/$1 > $T/$1 2>/dev/null
    rm -rf $T/x && mkdir $T/x
    ( cd $T/x && eval "$3" ) > $T/err 2>&1
    if ! diff -r $T/tree/proj $T/x/proj > $T/diff 2>&1; then
        echo "FAIL $2: $(cat $T/diff $T/err | head -3 | tr '\n' ' ')"; st=1; return
    fi
    bad=""
    for f in proj/README proj/src/main.c proj/src/blob.bin $D/file_with_a_long_name.txt; do
        [ "$(tm $T/tree/$f)" = "$(tm $T/x/$f)" ] || bad="$bad $f"
    done
    if [ "$4" != nolink ] && [ "$(ino $T/x/proj/README)" != "$(ino $T/x/proj/README.link)" ]; then
        bad="$bad (the hard link)"; fi
    if [ -n "$bad" ]; then echo "FAIL $2: times / modes differ:$bad"; st=1
    else echo "PASS $2"; fi
}
check back-ustar.tar "tar: a ustar archive from the host, unpacked and packed again on P32W (contents, modes, times, a hard link, a 120-character path)" "tar -xpf ../back-ustar.tar"
check back-pax.tar "tar: a pax archive from the host, unpacked and packed again on P32W" "tar -xpf ../back-pax.tar"
check back-tgz.tgz "tar z / gzip: a .tgz from the host, unpacked and packed again with tar czf on P32W" "tar -xzpf ../back-tgz.tgz"
check back-zip.zip "zip / unzip: an Info-ZIP archive from the host, unpacked and zipped again on P32W (read by the host's unzip)" "unzip -qX ../back-zip.zip" nolink
if [ -n "$DITTO" ]; then
check back-zip.zip "zip: P32W's zip archive opened by Finder's engine (ditto -x -k)" "ditto -x -k ../back-zip.zip . && python3 -c 'import zipfile,sys; sys.exit(zipfile.ZipFile(\"../back-zip.zip\").testzip() is not None)'" nolink
check back-ditto.tar "unzip: a Finder-style archive (ditto) from the host, unpacked on P32W" "tar -xpf ../back-ditto.tar" nolink
else echo "SKIP zip: P32W's zip archive opened by Finder's engine (ditto: macOS only)"
     echo "SKIP unzip: a Finder-style archive (ditto) unpacked on P32W (ditto: macOS only)"; fi
python3 $P cat $T/os.img /tmp/k.txt > $T/k.out 2>/dev/null
if cmp -s $T/k.out $T/tree/proj/src/kernel.txt; then echo "PASS gunzip: a file gzip -9 made on the host ($(wc -c < $T/tree/proj/src/kernel.txt | tr -d ' ') bytes)"
else echo "FAIL gunzip: $(grep -i gzip $T/log | head -2 | tr '\n' ' ')"; st=1; fi
if grep -q "No errors detected" $T/log; then echo "PASS unzip -t: P32W's own zip archive tests clean on P32W"
else echo "FAIL unzip -t: $(grep -i 'unzip' $T/log | tail -2 | tr '\n' ' ')"; st=1; fi
exit $st
