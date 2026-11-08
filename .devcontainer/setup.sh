#!/bin/sh
# setup.sh -- run when a codespace is made, and again when its container is rebuilt
# (devcontainer.json): the host's tools, both emulators, the test suite's disk
# (software/build/test.img; the tests never touch yours) and, if there is none yet, P32W
# Unix's disk, software/build/disk.img: never over yours, which keeps your files (a fresh
# one is make -C software disk, when you choose). About ten minutes the first time.
set -e
cd "$(dirname "$0")/.."
sudo apt-get update -qq
# cc and make for the emulators; python3 for the cross tools (cc, as, ld, p32fs); clang to
# preprocess MicroPython; zip, nc, expect and iverilog for parts of the test suite
sudo apt-get install -y -qq build-essential python3 clang zip unzip netcat-openbsd expect iverilog
make -s -C emulator p32emu p32uemu
make -s -C software testdisk
[ -f software/build/disk.img ] || make -s -C software disk
echo "P32W is ready: tools/p32w boots it (.devcontainer/README.md)"
