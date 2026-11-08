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

## The debugger, in the browser

```
python3 tools/p32dbg
```

P32W in the emulator with its screen, its console, the registers, the code,
memory and breakpoints on one page, which opens in a new tab by itself (GitHub
forwards the port, to an address only you can reach; the Ports tab has it
again). `python3 tools/p32dbg -u` runs the microcode-level emulator and shows
the CPU's insides cycle by cycle. Type `b` on the console (or press Run first)
to boot; click the screen to log in on it. `tools/p32dbg/README.md` says more.
Like `tools/p32w`, it uses your disk, `software/build/disk.img`; Quit on the
page ends it.

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
  out first. Nothing else does: rebuilding the container keeps it.
- **After a `git pull`:** `make -C emulator` builds the emulators again from the new
  sources (`tools/p32w` builds them only when they are missing).
- **The host side:** edit in VS Code, `make -C software` builds, `cd emulator && make test`
  runs every test (about an hour on this machine; three that need a Mac's own tools
  print SKIP). Commit and push as usual, and pull on the Macs.

## Good to know

- The codespace stops itself after 30 minutes idle (up to 4 hours: github.com/settings/codespaces).
  A running P32W stops with it; halt it first, so the disk is clean.
- GitHub's free monthly allowance covers some 60 hours on this 2-core machine (GitHub
  Free: 120 core-hours and 15 GB-months; Pro: 180 and 20; github.com/settings/billing).
  With no payment method on file, use stops there until the next month; with one, a $0
  Codespaces budget with "Stop usage when budget limit is reached" keeps it free. A
  stopped codespace still counts as storage: delete the ones you no longer use.
- KiCad is not here: the boards stay on the Macs.
