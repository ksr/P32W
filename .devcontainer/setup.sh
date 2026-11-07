#!/bin/sh
# setup.sh -- run once when a codespace is made (devcontainer.json): the host's tools,
# then both emulators and P32W Unix's disk, software/build/disk.img. About ten minutes.
set -e
cd "$(dirname "$0")/.."
sudo apt-get update -qq
# cc and make for the emulators; python3 for the cross tools (cc, as, ld, p32fs); clang to
# preprocess MicroPython; zip, nc, expect and iverilog for parts of the test suite
sudo apt-get install -y -qq build-essential python3 clang zip unzip netcat-openbsd expect iverilog
make -s -C emulator p32emu p32uemu
make -s -C software disk
echo "P32W is ready: tools/p32w boots it (.devcontainer/README.md)"
