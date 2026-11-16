# Author: Claude (Anthropic) for Ken Rother, 2026
"""MkDocs hook for the P32W website: a relative link to a repository file that is not on the site (a source, a board
file, a netlist) becomes a link to that file on GitHub (the repository is public since 2026-11-15; until then such
links were plain text), and an image that is not on the site is shown from GitHub's raw files. A link to a directory
goes to that directory's README page when the site has one, otherwise to the directory on GitHub; a link to the
repository README goes to "The repository" page, and one to .devcontainer/ to the codespace page."""
import os, posixpath, re
from urllib.parse import unquote

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # the repository this folder is in
GH = "https://github.com/ksr/P32W"
RAW = "https://raw.githubusercontent.com/ksr/P32W/main/"
LINK = re.compile(r'(!?)\[((?:[^\[\]]|\[[^\]]*\])*)\]\((<[^>]+>|[^)\s]+)((?:\s+"[^"]*")?)\)')
stats = {"github": 0, "readme": 0}


def on_page_markdown(markdown, page, config, files):
    site = {f.src_uri for f in files}
    here = posixpath.dirname(page.file.src_uri)

    def fix(m):
        bang, text, target, title = m.groups()
        t = target[1:-1] if target.startswith("<") else target
        if re.match(r"^[a-z][a-z0-9+.-]*:", t, re.I) or t.startswith("#") or t.startswith("/"): return m.group(0)
        path, _, frag = t.partition("#")
        if not path: return m.group(0)
        resolved = posixpath.normpath(posixpath.join(here, unquote(path)))
        if resolved in site or resolved.startswith(".."): return m.group(0)
        if resolved == ".": resolved = ""
        stand_in = {"": "repository.md", "README.md": "repository.md", ".devcontainer": "codespace.md",
                    ".devcontainer/README.md": "codespace.md"}.get(resolved)
        if not stand_in and os.path.isdir(os.path.join(REPO, resolved)):
            stand_in = posixpath.join(resolved, "README.md")
        if stand_in and stand_in in site and not bang:
            stats["readme"] += 1
            return "[%s](<%s>%s)" % (text, posixpath.relpath(stand_in, here or ".") + ("#" + frag if frag else ""), title)
        if not os.path.exists(os.path.join(REPO, resolved)): return m.group(0)       # leave MkDocs to report it
        stats["github"] += 1
        if bang:
            return "![%s](%s%s)" % (text, RAW + resolved, title)
        kind = "tree" if os.path.isdir(os.path.join(REPO, resolved)) else "blob"
        return "[%s](%s/%s/main/%s%s%s)" % (text, GH, kind, resolved, "#" + frag if frag else "", title)

    return LINK.sub(fix, markdown)


def on_post_build(config):
    print("hooks: %d links to files not on the site now point at GitHub; %d links to directories or the README go "
          "to their site page" % (stats["github"], stats["readme"]))
