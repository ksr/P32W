#!/bin/sh
# build_freerouting.sh -- build the Freerouting P32W routes with (D83): the
# official repository's master with two open pull requests merged, until a
# release has them:
#   #1021 (issue #1017): the router traded the last incomplete connections
#         between neighbouring nets pass after pass, and stalled;
#   #1055 (issue #1054): from pass 3 on, a route rejected for a clearance
#         fault left the connections it had ripped up unrouted.
# Writes ~/freerouting/p32w-fixes/freerouting.jar (build_board.sh's default)
# and BUILD.txt beside it. Needs git and a JDK (25 or later; Gradle fetches
# the Java 25 toolchain it builds with).
set -e
OUT=$HOME/freerouting/p32w-fixes
SRC=${TMPDIR:-/tmp}/freerouting-p32w
rm -rf "$SRC"
git clone -q https://github.com/freerouting/freerouting.git "$SRC"
cd "$SRC"
git fetch -q origin pull/1021/head:pr1021 pull/1055/head:pr1055
git checkout -q -b p32w-fixes
for pr in pr1021 pr1055; do
    git -c user.name=p32w -c user.email=p32w@localhost merge -q --no-edit "$pr"
done
JAVA_HOME=${JAVA_HOME:-$(/usr/libexec/java_home)} ./gradlew --no-daemon -q executableJar
mkdir -p "$OUT"
cp build/libs/freerouting-current-executable.jar "$OUT/freerouting.jar"
{ git log --oneline -3; echo "master + PR #1021 + PR #1055, built $(date '+%Y-%m-%d')"; } > "$OUT/BUILD.txt"
echo "-> $OUT/freerouting.jar"; cat "$OUT/BUILD.txt"
