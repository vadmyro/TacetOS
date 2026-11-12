#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 vadmyro
#
# Checks what building TacetOS needs and installs only what is missing.
# Linux (dnf, apt, pacman) and macOS (Homebrew). Windows: install-deps.ps1.
#
# Usage: scripts/install-deps.sh [--check]
#   --check  only report what is missing, install nothing.
#
# The package manager asks before it installs anything. When the Qt of the
# system is older than 6.8 (Ubuntu 24.04, Debian 12), Qt 6.8 is installed for
# this user only, with aqtinstall, in ~/Qt. No sudo for that part.

set -euo pipefail

QT_MIN=6.8
QT_FALLBACK=6.8.3
QT_DIR="$HOME/Qt"
CMAKE_MIN=3.21

check_only=0
case "${1:-}" in
    --check) check_only=1 ;;
    "") ;;
    *) echo "Usage: $0 [--check]" >&2; exit 2 ;;
esac

say() { printf '%s\n' "$*"; }
ok() { printf '  [ok]      %s\n' "$*"; }
missing() { printf '  [missing] %s\n' "$*"; }

# True when version $1 is at least $2 (both like 6.8.3 or 3.21).
version_ge() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]
}

os="$(uname -s)"
pm=""
case "$os" in
    Linux)
        if command -v dnf >/dev/null; then pm=dnf
        elif command -v apt-get >/dev/null; then pm=apt
        elif command -v pacman >/dev/null; then pm=pacman
        fi ;;
    Darwin)
        command -v brew >/dev/null && pm=brew ;;
    *)
        say "This script is for Linux and macOS. On Windows, run scripts/install-deps.ps1."
        exit 1 ;;
esac

# --- What is there --------------------------------------------------------

need_compiler=0 need_cmake=0 need_qt=0 need_qt_fallback=0 need_udev=0 need_python=0 need_git=0

say "Checking the build tools:"

if command -v c++ >/dev/null || command -v g++ >/dev/null || command -v clang++ >/dev/null; then
    ok "C++ compiler"
else
    missing "C++ compiler"; need_compiler=1
fi

if command -v cmake >/dev/null; then
    v="$(cmake --version | head -n1 | awk '{print $3}')"
    if version_ge "$v" "$CMAKE_MIN"; then ok "CMake $v"
    else missing "CMake $CMAKE_MIN or newer (found $v)"; need_cmake=1
    fi
else
    missing "CMake $CMAKE_MIN or newer"; need_cmake=1
fi

# Qt: the system one first, then one installed earlier by this script in ~/Qt.
qt_version=""
qt_prefix=""
for q in qmake6 qmake; do
    if command -v "$q" >/dev/null; then
        v="$("$q" -query QT_VERSION 2>/dev/null || true)"
        case "$v" in 6.*)
            qt_version="$v"
            qt_prefix="$("$q" -query QT_INSTALL_PREFIX)"
            break ;;
        esac
    fi
done
if [ -n "$qt_version" ] && ! version_ge "$qt_version" "$QT_MIN"; then
    old_qt="$qt_version"
    qt_version=""
fi
if [ -z "$qt_version" ]; then
    for d in "$QT_DIR"/6.*/gcc_64 "$QT_DIR"/6.*/macos; do
        if [ -x "$d/bin/qmake" ]; then
            v="$("$d/bin/qmake" -query QT_VERSION)"
            if version_ge "$v" "$QT_MIN"; then qt_version="$v"; qt_prefix="$d"; fi
        fi
    done
fi
if [ -n "$qt_version" ]; then
    if [ -d "$qt_prefix/lib/cmake/Qt6Svg" ] || [ -d "$qt_prefix/lib64/cmake/Qt6Svg" ] \
        || ls "$qt_prefix"/lib/*/cmake/Qt6Svg >/dev/null 2>&1; then
        ok "Qt $qt_version with Qt SVG"
    else
        missing "Qt SVG module (Qt $qt_version found)"; need_qt=1
    fi
elif [ -n "${old_qt:-}" ]; then
    missing "Qt $QT_MIN or newer (the system has $old_qt, too old)"; need_qt_fallback=1
else
    missing "Qt $QT_MIN or newer"; need_qt=1
fi

# On apt systems, the packaged Qt may itself be too old: use the fallback then.
if [ $need_qt = 1 ] && [ -z "$qt_version" ] && [ "$pm" = apt ]; then
    cand="$(apt-cache policy qt6-base-dev 2>/dev/null | awk '/Candidate:/ {print $2}')"
    cand="${cand#*:}"
    if [ -z "$cand" ] || [ "$cand" = "(none)" ] || ! version_ge "${cand%%[-+~]*}" "$QT_MIN"; then
        say "            (the Qt of this system is ${cand:-not available}: Qt $QT_FALLBACK will be installed for this user)"
        need_qt=0; need_qt_fallback=1
    fi
fi

if [ "$os" = Linux ]; then
    if command -v pkg-config >/dev/null && pkg-config --exists libudev; then
        ok "libudev (live plug and unplug)"
    else
        missing "libudev (live plug and unplug)"; need_udev=1
    fi
fi

if command -v python3 >/dev/null; then ok "Python 3 (tests)"; else missing "Python 3 (tests)"; need_python=1; fi
if command -v git >/dev/null; then ok "Git"; else missing "Git"; need_git=1; fi

# --- What to install --------------------------------------------------------

pkgs=()
add() { pkgs+=("$@"); }

case "$pm" in
    dnf)
        [ $need_compiler = 1 ] && add gcc-c++ make
        [ $need_cmake = 1 ] && add cmake
        [ $need_qt = 1 ] && add qt6-qtbase-devel qt6-qtsvg-devel
        [ $need_udev = 1 ] && add systemd-devel pkgconf-pkg-config
        [ $need_python = 1 ] && add python3
        [ $need_git = 1 ] && add git ;;
    apt)
        [ $need_compiler = 1 ] && add g++ make
        [ $need_cmake = 1 ] && add cmake
        [ $need_qt = 1 ] && add qt6-base-dev qt6-svg-dev
        [ $need_qt_fallback = 1 ] && add python3-venv libgl-dev libxkbcommon-dev
        [ $need_udev = 1 ] && add libudev-dev pkg-config
        [ $need_python = 1 ] && add python3
        [ $need_git = 1 ] && add git ;;
    pacman)
        [ $need_compiler = 1 ] && add gcc make
        [ $need_cmake = 1 ] && add cmake
        [ $need_qt = 1 ] && add qt6-base qt6-svg
        [ $need_udev = 1 ] && add systemd-libs pkgconf
        [ $need_python = 1 ] && add python
        [ $need_git = 1 ] && add git ;;
    brew)
        [ $need_cmake = 1 ] && add cmake
        [ $need_qt = 1 ] && add qt
        [ $need_python = 1 ] && add python
        [ $need_git = 1 ] && add git ;;
esac

any_missing=$((need_compiler + need_cmake + need_qt + need_qt_fallback + need_udev + need_python + need_git))

if [ $any_missing = 0 ]; then
    say ""
    say "Everything is there. Build with:"
    say "  cmake --preset debug && cmake --build --preset debug"
    [ -n "$qt_prefix" ] && case "$qt_prefix" in "$QT_DIR"/*)
        say "  (add -DCMAKE_PREFIX_PATH=$qt_prefix to the first command)" ;;
    esac
    exit 0
fi

if [ -z "$pm" ]; then
    say ""
    if [ "$os" = Darwin ]; then
        say "Homebrew is needed first: see https://brew.sh, then run this script again."
    else
        say "No supported package manager found (dnf, apt or pacman)."
        say "Install the missing items above with your own package manager."
    fi
    exit 1
fi

if [ $check_only = 1 ]; then
    say ""
    [ ${#pkgs[@]} -gt 0 ] && say "Would install with $pm: ${pkgs[*]}"
    [ $need_qt_fallback = 1 ] && say "Would install Qt $QT_FALLBACK for this user in $QT_DIR"
    [ "$os" = Darwin ] && [ $need_compiler = 1 ] && say "Would ask for the Xcode command line tools"
    exit 1
fi


say ""
if [ "$os" = Darwin ] && [ $need_compiler = 1 ]; then
    say "Installing the Xcode command line tools (a window opens):"
    xcode-select --install || true
fi

if [ ${#pkgs[@]} -gt 0 ]; then
    say "Installing: ${pkgs[*]}"
    case "$pm" in
        dnf) sudo dnf install "${pkgs[@]}" ;;
        apt) sudo apt-get update && sudo apt-get install "${pkgs[@]}" ;;
        pacman) sudo pacman -S --needed "${pkgs[@]}" ;;
        brew) brew install "${pkgs[@]}" ;;
    esac
fi

if [ $need_qt_fallback = 1 ]; then
    say "Installing Qt $QT_FALLBACK for this user in $QT_DIR (no sudo):"
    venv="$HOME/.local/share/tacetos/aqt"
    python3 -m venv "$venv"
    "$venv/bin/pip" install --quiet --upgrade aqtinstall
    "$venv/bin/aqt" install-qt linux desktop "$QT_FALLBACK" linux_gcc_64 --outputdir "$QT_DIR"
    qt_prefix="$QT_DIR/$QT_FALLBACK/gcc_64"
fi

say ""
say "Done. Build with:"
if [ $need_qt_fallback = 1 ]; then
    say "  cmake --preset debug -DCMAKE_PREFIX_PATH=$qt_prefix && cmake --build --preset debug"
else
    say "  cmake --preset debug && cmake --build --preset debug"
fi
