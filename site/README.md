# lantern site — famemu.aulenor.com

The marketing page (`index.html`) and developer docs (`docs/index.html`) for
the lantern engine. Static, self-contained, no build step, no external
requests (system fonts, inline favicon, local screenshots).

```
site/
├── index.html        # marketing landing page
├── docs/index.html   # developer docs (full API reference)
├── style.css         # shared design system (dark, mono accents, hairlines)
├── assets/*.png      # REAL engine frames from docs/img — never mockups
├── 404.html  robots.txt  sitemap.xml
├── CNAME             # famemu.aulenor.com (GitHub Pages custom domain)
└── deploy/deploy-website.yml   # ready-to-copy GH Actions workflow
```

## Preview locally

The repo's `.claude/launch.json` has a `lantern-site` server, or:

```sh
python3 -m http.server 8341 -d engine/site
```

## Deploy — the house pattern (same as quenderin.aulenor.com)

GitHub Pages via Actions. One-time setup:

1. `git init` the famicom repo (or a dedicated `famemu-site` repo), commit,
   push to GitHub.
2. Copy `deploy/deploy-website.yml` to `.github/workflows/` (its `path:`
   points at `engine/site`). It self-enables Pages on first run.
3. DNS at aulenor.com: add a CNAME record
   `famemu` → `<github-user>.github.io`, then in repo Settings → Pages set
   the custom domain to `famemu.aulenor.com` (the `CNAME` file here keeps it
   pinned across deploys) and tick Enforce HTTPS.

Any other static host (Cloudflare Pages, Netlify) also works: point it at
`engine/site` as the publish directory — `/docs` works because it's a real
directory with an `index.html`.

## Updating

- Screenshots come from `engine/docs/img/` (which come from LANTERN_SHOT
  runs). Copy new ones into `assets/` — never fake a frame.
- The API tables in `docs/index.html` mirror `docs/ENGINE.md` and
  `include/lantern.h`. When the API changes, update all three (the header
  is the source of truth).
