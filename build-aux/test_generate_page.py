#!/usr/bin/env python3
"""
Tests for generate-page.py.  Run:  python3 build-aux/test_generate_page.py

(The old preview helper "test-generate-page.py" is a bash script despite its
extension; it still works and is still the quickest way to eyeball the page.)
"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "generate-page.py")
BASE = "https://github.com/o/r/releases/download/development-x"

ALL_ASSETS = "\n".join([
    "QElectroTech-0.200.1-dev-r800-abc1234_x86_64-win64.exe",
    "qelectrotech-0.200.1-dev-r800-abc1234_x86_64-win64-readytouse.zip",
    "QElectroTech-0.200.1-dev-r800-abc1234_x86_64-win64.msi",
    "QElectroTech-0.200.1-dev-r800-abc1234-arm64.dmg",
    "QElectroTech-0.200.1-dev-r800-abc1234-x86_64.dmg",
    "QElectroTech-0.200.1-dev-r800-abc1234-x86_64.AppImage",
    "QElectroTech-0.200.1-dev-r800-abc1234-aarch64.AppImage",
    "qelectrotech_0.200.1-dev-r800-abc1234_amd64.snap",
    "qelectrotech_0.200.1-dev-r800-abc1234_arm64.snap",
    "qelectrotech-0.200.1-dev-r800-abc1234-x86_64.flatpak",
    "qelectrotech-0.200.1-dev-r800-abc1234-aarch64.flatpak",
])

ALL_OK = {j: "success" for j in (
    "build-macos", "build-appimage", "build-windows-exe", "build-windows-msi",
    "build-linux-snap", "build-linux-flatpack", "build-docs")}


def render(extra_env):
    env = {
        "PATH": os.environ.get("PATH", ""),
        "DATE": "2026-09-28 10:00 UTC", "SHORT": "abc1234", "REPO": "o/r",
        "SHA": "abc1234" + "0" * 33, "RUN_URL": "https://example/run/1",
        "RUN_NUMBER": "7", "RELEASE_TAG": "development-x",
    }
    env.update(extra_env)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([sys.executable, SCRIPT], cwd=tmp, env=env, check=True,
                       stdout=subprocess.DEVNULL)
        with open(os.path.join(tmp, "gh-pages", "index.html"), encoding="utf-8") as f:
            return f.read()


class AssetMapping(unittest.TestCase):
    def test_every_button_found_from_asset_names(self):
        html = render({"ASSET_NAMES": ALL_ASSETS, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(ALL_OK)})
        for name in ALL_ASSETS.splitlines():
            self.assertIn(f'href="{BASE}/{name}"', html, name)
        self.assertNotIn("btn-failed\"", html)      # no failed buttons rendered
        self.assertNotIn("Not everything could be built", html)
        self.assertIn('<a href="docs/">API documentation</a>', html)

    def test_aarch64_and_x86_do_not_swap(self):
        html = render({"ASSET_NAMES": ALL_ASSETS, "BASE_URL": BASE})
        # the x86_64 AppImage button must point at the x86_64 file
        i = html.index("Linux x86_64 AppImage")
        self.assertIn("x86_64.AppImage", html[i - 200:i])
        j = html.index("Linux aarch64 AppImage")
        self.assertIn("aarch64.AppImage", html[j - 200:j])

    def test_explicit_url_wins(self):
        html = render({"ASSET_NAMES": ALL_ASSETS, "BASE_URL": BASE,
                       "MSI_URL": "https://elsewhere/x.msi"})
        self.assertIn('href="https://elsewhere/x.msi"', html)

    def test_old_env_only_interface_still_works(self):
        html = render({"INSTALLER_URL": "https://h/a.exe", "DMG_ARM64_URL": "https://h/a.dmg"})
        self.assertIn('href="https://h/a.exe"', html)
        self.assertIn('href="https://h/a.dmg"', html)
        self.assertNotIn("AppImage", html)          # no URL, no status: left out


class FailedPackages(unittest.TestCase):
    def test_failed_package_is_marked_and_others_stay(self):
        assets = "\n".join(a for a in ALL_ASSETS.splitlines() if not a.endswith(".dmg"))
        status = dict(ALL_OK, **{"build-macos": "failure"})
        html = render({"ASSET_NAMES": assets, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(status)})
        self.assertIn("macOS Apple Silicon (arm64)", html)
        self.assertIn("build failed", html)
        self.assertIn("Not everything could be built this time: macOS DMG", html)
        self.assertIn('href="https://example/run/1"', html)
        # the other platforms are still complete
        self.assertIn(f'href="{BASE}/QElectroTech-0.200.1-dev-r800-abc1234-x86_64.AppImage"', html)
        self.assertIn(f'href="{BASE}/QElectroTech-0.200.1-dev-r800-abc1234_x86_64-win64.msi"', html)

    def test_partial_matrix_failure_only_marks_missing_arch(self):
        assets = "\n".join(a for a in ALL_ASSETS.splitlines() if "aarch64.AppImage" not in a)
        status = dict(ALL_OK, **{"build-appimage": "failure"})
        html = render({"ASSET_NAMES": assets, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(status)})
        self.assertIn(f'href="{BASE}/QElectroTech-0.200.1-dev-r800-abc1234-x86_64.AppImage"', html)
        j = html.index("Linux aarch64 AppImage")
        self.assertIn("btn-failed", html[j - 200:j])

    def test_msi_skipped_because_exe_failed(self):
        assets = "\n".join(a for a in ALL_ASSETS.splitlines()
                           if not a.endswith((".exe", ".zip", ".msi")))
        status = dict(ALL_OK, **{"build-windows-exe": "failure", "build-windows-msi": "skipped"})
        html = render({"ASSET_NAMES": assets, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(status)})
        self.assertIn("not built (a step it depends on failed)", html)
        self.assertIn("Windows installer + portable", html)   # named in the banner

    def test_success_but_file_missing_is_flagged(self):
        assets = "\n".join(a for a in ALL_ASSETS.splitlines() if not a.endswith(".flatpak"))
        html = render({"ASSET_NAMES": assets, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(ALL_OK)})
        self.assertIn("built, but the file is missing from the release", html)

    def test_docs_link_only_when_docs_built(self):
        html = render({"ASSET_NAMES": ALL_ASSETS, "BASE_URL": BASE,
                       "PACKAGE_STATUS": json.dumps(dict(ALL_OK, **{"build-docs": "failure"}))})
        self.assertNotIn('href="docs/"', html)
        self.assertIn("API documentation (not built this time)", html)

    def test_garbage_status_does_not_crash(self):
        html = render({"ASSET_NAMES": ALL_ASSETS, "BASE_URL": BASE, "PACKAGE_STATUS": "{not json"})
        self.assertIn("Windows Installer", html)


if __name__ == "__main__":
    unittest.main(verbosity=2)
