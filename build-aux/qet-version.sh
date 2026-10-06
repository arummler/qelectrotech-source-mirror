#!/usr/bin/env bash
# qet-version.sh -- the ONE place that turns QET_VERSION + QET_RELEASE + git
# into every version string the CI and the packaging scripts need.
#
# Before this script the same "<version><suffix>" rule was re-implemented in
# ~10 places (package.yml, the exe/msi/flatpak/snap/doxygen workflows, the
# AppImage and DMG scripts, snapcraft.yaml, ...) and had drifted apart: the
# same commit produced -r<count>-<sha> on Windows, -r<sha> on macOS/Linux,
# -g<sha8> in the snap and -dev in the docs. Everything now derives from here.
#
# Usage:
#   build-aux/qet-version.sh [--source-dir DIR] [--format env|github|json]
#
#   env     KEY=value lines, meant for:  eval "$(build-aux/qet-version.sh)"
#   github  lower-case key=value lines appended to $GITHUB_OUTPUT (stdout if unset)
#   json    one JSON object (for pwsh:  bash build-aux/qet-version.sh --format json | ConvertFrom-Json)
#
# Inputs
#   QET_VERSION   file at the source root, e.g. 0.200.1
#   QET_RELEASE   file at the source root: dev | alpha1 | alpha2 | alpha3 | stable
#   git history   revision count and commit id
#
# Optional environment overrides (for shallow clones and source tarballs):
#   QET_REV       final revision number, used instead of counting commits
#   QET_SHA       commit id, used instead of asking git
#
# Keys
#   VERSION          0.200.1
#   RELEASE          dev
#   REV              commit count + REV_OFFSET  (e.g. 1234)
#   SHA / SHORT_SHA  full / 7-character commit id
#   SUFFIX           dev: -dev-r<REV>-<SHORT_SHA>   alphaN: -alphaN   stable: (empty)
#   DISPLAY_VERSION  VERSION+SUFFIX -- what file names, the release page, the
#                    snap/flatpak version and the MSI display name use
#   LABEL            dev: VERSION-dev   alphaN: VERSION-alphaN   stable: VERSION
#                    -- the short human label (doxygen PROJECT_NUMBER)
#   VCS_TAG          dev: +git<REV>     otherwise empty (portable zip file name)
#   MSI_VERSION      MAJOR.MINOR.(REV % 65535): Windows Installer only compares
#                    the first three fields, so REV must move the third one
#   IS_PRERELEASE    true unless RELEASE=stable
#   TAG_NAME         the git tag a release of this file state must carry:
#                    v<VERSION> (stable) or v<VERSION>-<RELEASE>
#   SNAP_CHANNEL     dev: edge  alpha1/2: beta  alpha3: candidate  stable: stable

set -euo pipefail

# Legacy offset from the Windows packaging scripts (continuity with the
# revision numbers already published). Do NOT change it without a reason: it
# would make the next build's revision go backwards.
REV_OFFSET=473

FORMAT="env"
SOURCE_DIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --format)     FORMAT="${2:?--format needs a value}"; shift 2 ;;
    --format=*)   FORMAT="${1#*=}"; shift ;;
    --source-dir) SOURCE_DIR="${2:?--source-dir needs a value}"; shift 2 ;;
    --source-dir=*) SOURCE_DIR="${1#*=}"; shift ;;
    -h|--help)    sed -n '2,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "qet-version.sh: unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$SOURCE_DIR" ]; then
  SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fi

read_file() {
  [ -f "$SOURCE_DIR/$1" ] || { echo "qet-version.sh: missing $SOURCE_DIR/$1" >&2; exit 1; }
  tr -d '[:space:]' < "$SOURCE_DIR/$1"
}

VERSION="$(read_file QET_VERSION)"
RELEASE="$(read_file QET_RELEASE)"

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "qet-version.sh: QET_VERSION '$VERSION' is not MAJOR.MINOR.PATCH" >&2
  exit 1
fi

# --- git -------------------------------------------------------------------
in_git=false
if git -C "$SOURCE_DIR" rev-parse --git-dir >/dev/null 2>&1; then
  in_git=true
elif [ -e "$SOURCE_DIR/.git" ] && [ -z "${QET_REV:-}${QET_SHA:-}" ]; then
  # A checkout git cannot read (typically "dubious ownership" in a container)
  # must not silently become r0-0000000.
  echo "qet-version.sh: $SOURCE_DIR is a git checkout, but git cannot read it:" >&2
  git -C "$SOURCE_DIR" rev-parse --git-dir >&2 || true
  echo "qet-version.sh: in a container, run: git config --global --add safe.directory \"$SOURCE_DIR\"" >&2
  exit 1
fi

if [ -n "${QET_SHA:-}" ]; then
  SHA="$QET_SHA"
elif $in_git; then
  SHA="$(git -C "$SOURCE_DIR" rev-parse HEAD)"
else
  SHA="0000000000000000000000000000000000000000"
  echo "qet-version.sh: not a git checkout, using a placeholder commit id" >&2
fi
SHORT_SHA="${SHA:0:7}"

if [ -n "${QET_REV:-}" ]; then
  REV="$QET_REV"
elif $in_git; then
  if [ "$(git -C "$SOURCE_DIR" rev-parse --is-shallow-repository)" = "true" ]; then
    echo "qet-version.sh: shallow clone -- the commit count is wrong. Use fetch-depth: 0 or set QET_REV." >&2
    exit 1
  fi
  REV=$(( $(git -C "$SOURCE_DIR" rev-list HEAD --count) + REV_OFFSET ))
else
  REV=0
  echo "qet-version.sh: not a git checkout, using REV=0" >&2
fi

# --- release-type rules ----------------------------------------------------
case "$RELEASE" in
  dev)
    SUFFIX="-dev-r${REV}-${SHORT_SHA}"; LABEL="${VERSION}-dev"
    VCS_TAG="+git${REV}";           SNAP_CHANNEL="edge"
    ;;
  alpha1|alpha2)
    SUFFIX="-${RELEASE}";           LABEL="${VERSION}-${RELEASE}"
    VCS_TAG="";                     SNAP_CHANNEL="beta"
    ;;
  alpha3)
    SUFFIX="-${RELEASE}";           LABEL="${VERSION}-${RELEASE}"
    VCS_TAG="";                     SNAP_CHANNEL="candidate"
    ;;
  stable)
    SUFFIX="";                      LABEL="${VERSION}"
    VCS_TAG="";                     SNAP_CHANNEL="stable"
    ;;
  *)
    echo "qet-version.sh: QET_RELEASE '$RELEASE' must be dev, alpha1, alpha2, alpha3 or stable" >&2
    exit 1
    ;;
esac

DISPLAY_VERSION="${VERSION}${SUFFIX}"

IFS=. read -r V_MAJOR V_MINOR _V_PATCH <<< "$VERSION"
MSI_VERSION="${V_MAJOR}.${V_MINOR}.$(( REV % 65535 ))"

if [ "$RELEASE" = "stable" ]; then
  IS_PRERELEASE=false;  TAG_NAME="v${VERSION}"
else
  IS_PRERELEASE=true;   TAG_NAME="v${VERSION}-${RELEASE}"
fi

KEYS="VERSION RELEASE REV SHA SHORT_SHA SUFFIX DISPLAY_VERSION LABEL VCS_TAG MSI_VERSION IS_PRERELEASE TAG_NAME SNAP_CHANNEL"

emit() {
  local k
  case "$FORMAT" in
    env)
      for k in $KEYS; do printf '%s=%s\n' "$k" "${!k}"; done
      ;;
    github)
      for k in $KEYS; do
        printf '%s=%s\n' "$(printf '%s' "$k" | tr 'A-Z' 'a-z')" "${!k}"
      done
      ;;
    json)
      local first=true
      printf '{'
      for k in $KEYS; do
        $first || printf ','
        first=false
        printf '"%s":"%s"' "$k" "${!k}"
      done
      printf '}\n'
      ;;
    *) echo "qet-version.sh: unknown --format '$FORMAT'" >&2; exit 2 ;;
  esac
}

if [ "$FORMAT" = "github" ] && [ -n "${GITHUB_OUTPUT:-}" ]; then
  emit >> "$GITHUB_OUTPUT"
else
  emit
fi
