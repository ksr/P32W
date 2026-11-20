# website/ — the P32W project website

The project website is built from this repository's own documents with [MkDocs](https://www.mkdocs.org/) and the
[Material](https://squidfunk.github.io/mkdocs-material/) theme, the same way as the YACC1, P8X and System-5 sites. The
documents stay where they are; the site is a readable view of them, with navigation and search. Its address is
`https://p32w.cottageworker.com`.

| File | What |
|---|---|
| `home.md` | the visitor home page (the site's index) |
| `stage.py` | copies what the site uses into `stage/`, keeping repository paths so the links between documents work: the design documents, the PDF books (when `tools/mkpdf.py` has made them), the READMEs, each card's schematic and placement PDFs; and writes one "Board" page per card (`hardware/<card>/board.md`) with its KiCad renders, resized and turned so the bus connectors are at the bottom |
| `hooks.py` | MkDocs hook: links to repository files that are not on the site go to those files on GitHub (the repository is public since 2026-11-15; until then they were plain text) |
| `mkdocs.yml` | the site: navigation, theme, Markdown extensions |
| `mkdocs-publish.yml` | `mkdocs.yml` + the Google Analytics tag (G-X7P96DTCNE); only the published build carries it |
| `requirements.txt` | the Python packages, pinned |
| `build.sh` | stage + build (installs the packages into `venv/` on the first run, which needs the network) |

    website/build.sh            build into website/site/
    website/build.sh serve      ... and serve it at http://127.0.0.1:8768 (Ctrl-C stops it)
    website/build.sh publish    the build to publish (with Google Analytics)

`venv/`, `stage/` and `site/` are rebuilt each time and not committed.

**Never on the site:** `software/micropython/` (third-party), `software/build/`, `CLAUDE.md`.

**Publishing.** `.github/workflows/website.yml` runs on every push to `main` (or by hand: Actions -> website -> Run
workflow): it makes the PDF books (`tools/mkpdf.py`, headless Chrome) and builds `website/build.sh publish` on GitHub's machine, so every push proves the site builds. It deploys
to GitHub Pages only when the repository variable `PAGES_DEPLOY` is `true` (Settings -> Secrets and variables ->
Actions -> Variables) and Pages is on (Settings -> Pages -> Source: GitHub Actions). Both are set (`PAGES_DEPLOY` since
2026-11-13), with the custom domain `p32w.cottageworker.com` in Settings -> Pages and a CNAME record `p32w` ->
`ksr.github.io` at Gandi.
