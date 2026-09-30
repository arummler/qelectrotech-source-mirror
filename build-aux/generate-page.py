#!/usr/bin/env python3
"""
generate-page.py -- Generates gh-pages/index.html for QElectroTech development builds.

Called from package.yml (publish-release job).

Required environment variables:
  DATE, SHORT, REPO, SHA, RUN_URL, RUN_NUMBER, RELEASE_TAG

How the download buttons are found (either way works, they can be mixed):
  ASSET_NAMES + BASE_URL   the file names of the release (one per line) and the
                           URL prefix; every button picks its file by extension
                           and architecture (see BUTTONS below)
  <NAME>_URL               an explicit URL for one button, wins over the above:
                           INSTALLER_URL PORTABLE_URL MSI_URL
                           DMG_ARM64_URL DMG_X8664_URL
                           APPIMAGE_X8664_URL APPIMAGE_AARCH64_URL
                           SNAP_AMD64_URL SNAP_ARM64_URL
                           FLATPAK_X8664_URL FLATPAK_AARCH64_URL

Optional:
  PACKAGE_STATUS   JSON {"<package.yml job id>": "success|failure|cancelled|skipped"}
                   With it, a package that did not build is shown as FAILED on
                   the page (with a link to the run) instead of silently
                   vanishing, and the API-documentation link is only offered
                   when the docs were built.
                   Without it, a button without a file is simply left out.
"""
import json
import os

env = os.environ.get

date        = env("DATE", "")
short       = env("SHORT", "")
repo        = env("REPO", "")
sha         = env("SHA", "")
run_url     = env("RUN_URL", "")
run_number  = env("RUN_NUMBER", "")
release_tag = env("RELEASE_TAG", "")

base_url = env("BASE_URL", "").rstrip("/")
assets = [a.strip() for a in env("ASSET_NAMES", "").splitlines() if a.strip()]
try:
    status = json.loads(env("PACKAGE_STATUS", "") or "{}")
except json.JSONDecodeError:
    status = {}


def has(ext, *, arch=None, not_arch=None):
    """Predicate on an asset file name."""
    def pred(name):
        if not name.endswith(ext):
            return False
        if arch is not None and arch not in name:
            return False
        if not_arch is not None and not_arch in name:
            return False
        return True
    return pred


# (env name, package.yml job that produces it, predicate, css class, icon, label, small text)
BUTTONS = {
    "windows": [
        ("INSTALLER_URL", "build-windows-exe", has(".exe"), "btn-primary", "&#11015;",
         "Windows Installer", ".exe &mdash; recommended, includes all dependencies"),
        ("MSI_URL", "build-windows-msi", has(".msi"), "btn-msi", "&#11015;",
         "Windows Installer .msi", ".msi &mdash; for enterprise / GPO deployment"),
        ("PORTABLE_URL", "build-windows-exe", has(".zip"), "btn-secondary", "&#128230;",
         "Windows Portable",
         '.zip &mdash; no installation required, extract and run &quot;Lancer QET.bat&quot;'),
    ],
    "macos": [
        ("DMG_ARM64_URL", "build-macos", has(".dmg", arch="arm64"), "btn-primary", "&#11015;",
         "macOS Apple Silicon (arm64)", ".dmg &mdash; for M1/M2/M3/M4 Macs"),
        ("DMG_X8664_URL", "build-macos", has(".dmg", arch="x86_64"), "btn-secondary", "&#11015;",
         "macOS Intel (x86_64)", ".dmg &mdash; for Intel-based Macs"),
    ],
    "appimage": [
        ("APPIMAGE_X8664_URL", "build-appimage", has(".AppImage", not_arch="aarch64"), "btn-primary", "&#11015;",
         "Linux x86_64 AppImage", ".AppImage &mdash; chmod +x and run, no installation required"),
        ("APPIMAGE_AARCH64_URL", "build-appimage", has(".AppImage", arch="aarch64"), "btn-secondary", "&#11015;",
         "Linux aarch64 AppImage",
         ".AppImage &mdash; chmod +x and run, no installation required"),
    ],
    "snap": [
        ("SNAP_AMD64_URL", "build-linux-snap", has(".snap", arch="amd64"), "btn-primary", "&#11015;",
         "Linux amd64 Snap", ".snap &mdash; sudo snap install --dangerous ./&lt;file&gt;.snap"),
        ("SNAP_ARM64_URL", "build-linux-snap", has(".snap", arch="arm64"), "btn-secondary", "&#11015;",
         "Linux arm64 Snap", ".snap &mdash; sudo snap install --dangerous ./&lt;file&gt;.snap"),
    ],
    "flatpak": [
        ("FLATPAK_X8664_URL", "build-linux-flatpack", has(".flatpak", not_arch="aarch64"), "btn-primary", "&#11015;",
         "Linux x86_64 Flatpak", ".flatpak &mdash; flatpak install ./&lt;file&gt;.flatpak"),
        ("FLATPAK_AARCH64_URL", "build-linux-flatpack", has(".flatpak", arch="aarch64"), "btn-secondary", "&#11015;",
         "Linux aarch64 Flatpak", ".flatpak &mdash; flatpak install ./&lt;file&gt;.flatpak"),
    ],
}

# platform key -> (heading, extra html shown above the buttons)
CARDS = {
    "windows":  ("&#127993; Windows &mdash; x86_64", ""),
    "macos":    ("&#127838; macOS", ""),
    "appimage": ("&#128039; Linux &mdash; AppImage", ""),
    "snap":     ("&#128230; Linux &mdash; Snap", """<div class="warning">
&#9888;&#65039; Not yet published to the Snap Store &mdash; this is a raw, unsigned
bundle. Requires the <code>--dangerous</code> flag to install manually, since
it isn't signed by the Store.
</div>"""),
    "flatpak":  ("&#128230; Linux &mdash; Flatpak", ""),
}

# friendly names for the failure banner
JOB_NAMES = {
    "build-macos": "macOS DMG", "build-appimage": "Linux AppImage",
    "build-windows-exe": "Windows installer + portable", "build-windows-msi": "Windows MSI",
    "build-linux-snap": "Linux Snap", "build-linux-flatpack": "Linux Flatpak",
    "build-docs": "API documentation",
}

WHY = {
    "failure":   "build failed",
    "cancelled": "build cancelled",
    "skipped":   "not built (a step it depends on failed)",
}


def find_url(env_name, pred):
    explicit = env(env_name, "")
    if explicit:
        return explicit
    if base_url:
        for name in assets:
            if pred(name):
                return f"{base_url}/{name}"
    return ""


def render_button(env_name, job, pred, css, icon, label, small):
    url = find_url(env_name, pred)
    if url:
        return f"""
<a class="btn {css}" href="{url}">
<span class="btn-icon">{icon}</span>
<span class="btn-text">{label}<small>{small}</small></span>
</a>"""
    state = status.get(job)
    if state in WHY:
        why = WHY[state]
    elif state == "success":
        why = "built, but the file is missing from the release"
    else:
        return ""   # no status information: leave the button out
    return f"""
<div class="btn btn-failed">
<span class="btn-icon">&#10060;</span>
<span class="btn-text">{label}<small>{why} &mdash; <a href="{run_url}">see the run</a></small></span>
</div>"""


cards = ""
for key, buttons in BUTTONS.items():
    rendered = "".join(render_button(*b) for b in buttons)
    if not rendered:
        continue
    heading, extra = CARDS[key]
    cards += f"""
<div class="card">
<h2>{heading}</h2>
{extra}
<div class="downloads">
{rendered}
</div>
</div>"""

failed_jobs = [JOB_NAMES.get(j, j) for j, s in status.items() if s != "success"]
failure_banner = ""
if failed_jobs:
    failure_banner = f"""<div class="warning">
&#10060; Not everything could be built this time: {", ".join(failed_jobs)}.
The other downloads below are complete. <a href="{run_url}">See the CI run</a>.
</div>"""

docs_state = status.get("build-docs")
if docs_state in (None, "success"):
    docs_link = '<a href="docs/">API documentation</a>'
else:
    docs_link = "API documentation (not built this time)"

html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>QElectroTech &ndash; Development Builds</title>
<style>
*,*::before,*::after{{box-sizing:border-box;margin:0;padding:0}}
body{{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#f0f4f8;color:#2d3748;min-height:100vh}}
header{{background:linear-gradient(135deg,#1a365d 0%,#2b6cb0 100%);color:white;padding:48px 24px 40px;text-align:center}}
header h1{{font-size:2.2em;letter-spacing:-0.5px;margin-bottom:8px}}
header p{{opacity:.8;font-size:1.05em}}
main{{max-width:680px;margin:40px auto;padding:0 20px 60px}}
.card{{background:white;border-radius:12px;padding:28px;margin-bottom:24px;box-shadow:0 2px 12px rgba(0,0,0,.08)}}
.card h2{{font-size:1em;text-transform:uppercase;letter-spacing:.06em;color:#718096;margin-bottom:16px}}
.meta{{font-size:.875em;color:#4a5568;line-height:1.8;margin-bottom:20px}}
.meta a{{color:#2b6cb0;text-decoration:none}}
.meta a:hover{{text-decoration:underline}}
.badge{{display:inline-block;background:#ebf8ff;color:#2b6cb0;border-radius:4px;font-size:.8em;font-weight:600;padding:2px 8px;margin-left:6px;vertical-align:middle}}
.warning{{background:#fffbeb;border-left:4px solid #f6ad55;border-radius:4px;padding:12px 16px;font-size:.875em;color:#744210;margin-bottom:24px;line-height:1.5}}
.warning a{{color:#c05621}}
.downloads{{display:flex;flex-direction:column;gap:12px}}
.btn{{display:flex;align-items:center;gap:12px;padding:14px 20px;border-radius:8px;font-size:.95em;font-weight:600;text-decoration:none;transition:transform .1s,box-shadow .1s}}
.btn:hover{{transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,0,0,.15)}}
.btn-primary{{background:#2b6cb0;color:white}}
.btn-msi{{background:#6b46c1;color:white}}
.btn-secondary{{background:#edf2f7;color:#2d3748}}
.btn-failed{{background:#fff5f5;color:#9b2c2c;border:1px dashed #fc8181;cursor:default}}
.btn-failed:hover{{transform:none;box-shadow:none}}
.btn-failed a{{color:#9b2c2c;text-decoration:underline}}
.btn-icon{{font-size:1.3em}}
.btn-text small{{display:block;font-weight:400;font-size:.8em;opacity:.75;margin-top:1px}}
footer{{text-align:center;font-size:.8em;color:#4a5568;padding:32px 0 0}}
footer a{{color:#2d3748;text-decoration:none}}
</style>
</head>
<body>
<header>
<h1>&#9889; QElectroTech</h1>
<p>Development Builds</p>
</header>
<main>
<div class="card">
<h2>Build info</h2>
<div class="meta">
&#128197; &nbsp;<strong>{date}</strong><br>
&#128256; &nbsp;Commit <a href="https://github.com/{repo}/commit/{sha}"><code>{short}</code></a><br>
&#128295; &nbsp;<a href="{run_url}">CI Run #{run_number}</a>
<span class="badge">development</span>
</div>
<div class="warning">
&#9888;&#65039; This is a development version generated automatically from the newest commit on the master branch, It might introduce new features which you might want, but it may also exhibit new bugs that have not yet been identified yet.
For production use, download a <a href="https://github.com/{repo}/releases">stable release</a>.
</div>
{failure_banner}
<a class="btn btn-secondary" href="https://github.com/{repo}/releases/tag/{release_tag}">
<span class="btn-icon">&#128230;</span>
<span class="btn-text">All development version binaries on GitHub<small>Every platform &mdash; release page with checksums</small></span>
</a>
</div>
{cards}
</main>
<footer>
Auto-generated by GitHub Actions &nbsp;&middot;&nbsp;
<a href="https://github.com/{repo}">Source on GitHub</a> &nbsp;&middot;&nbsp;
<a href="https://qelectrotech.org">qelectrotech.org</a> &nbsp;&middot;&nbsp;
{docs_link}
</footer>
</body>
</html>"""

os.makedirs("gh-pages", exist_ok=True)
with open("gh-pages/index.html", "w", encoding="utf-8") as f:
    f.write(html)

print("index.html written OK")
