# P32W in a codespace

P32W's tools and emulators on a Linux machine of GitHub's, used from a web browser
(a Chromebook will do): VS Code in the browser tab, with a terminal. Nothing to install.

## Opening it

- **The first time:** github.com/codespaces, **New codespace**, repository `ksr/P32W`,
  branch `main`, **Create**. Setting up takes about ten minutes, once: it builds both
  emulators and P32W Unix's disk (`.devcontainer/setup.sh`).
- **After that:** github.com/codespaces and click it. It picks up where you left off.

## Running P32W

In the terminal (Terminal, New Terminal):

```
tools/p32w
```

At the monitor's `>` type `b` to boot. Log in as `ken` (password `p32`) or `root` (`root`).
`halt` shuts P32W down cleanly; **Ctrl+]** stops the emulator at once. `tools/p32w -u` runs
the microcode-level emulator instead (slower, the real control store).

**A second user** at once: `tools/p32w -T 2323`, then in a second terminal
`python3 tools/p32term.py`.

## Your work

- **On P32W:** the disk, `software/build/disk.img`, keeps its files between sessions, even
  after the codespace stops. It is not in git.
- **Getting a file out**, to commit it: `sync` (or `halt`) in P32W first, then in a terminal
  ```
  python3 tools/p32fs.py cat software/build/disk.img /home/ken/prog.c > prog.c
  ```
- **Putting a file in,** with P32W halted:
  ```
  python3 tools/p32fs.py put software/build/disk.img prog.c /home/ken/prog.c
  ```
- **Careful:** `make -C software disk` builds a fresh disk over this one. Copy your files
  out first.
- **The host side:** edit in VS Code, `make -C software` builds, `cd emulator && make test`
  runs every test (about an hour on this machine). Commit and push as usual, and pull on
  the Macs.

## Good to know

- The codespace stops itself after 30 minutes idle (up to 4 hours: github.com/settings/codespaces).
  A running P32W stops with it; halt it first, so the disk is clean.
- GitHub's free monthly allowance covers some 60 hours on this 2-core machine
  (github.com/settings/billing).
- KiCad is not here: the boards stay on the Macs.
