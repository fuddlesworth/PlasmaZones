#!/bin/bash
# SPDX-FileCopyrightText: 2026 abirkel <abirkel@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-or-later
# PlasmaZones Web Installer
# Fetches the latest portable tarball release and installs it.

set -e

REPO="fuddlesworth/PlasmaZones"
ARTIFACT_PATTERN="linux-x86_64.tar.gz"

# Check for required dependencies
if ! command -v jq >/dev/null 2>&1; then
    echo "Error: jq is required but not installed."
    echo "Please install jq and try again:"
    echo "  - Debian/Ubuntu: sudo apt install jq"
    echo "  - Fedora: sudo dnf install jq"
    echo "  - Arch: sudo pacman -S jq"
    exit 1
fi

echo "Finding latest release for $REPO..."
API_URL="https://api.github.com/repos/$REPO/releases/latest"
DOWNLOAD_URL=$(curl -s "$API_URL" | jq -r ".assets[] | select(.name | contains(\"$ARTIFACT_PATTERN\")) | .browser_download_url" | head -n 1)

if [ -z "$DOWNLOAD_URL" ]; then
    echo "Error: Could not find latest release artifact matching '$ARTIFACT_PATTERN'."
    echo "Please check https://github.com/$REPO/releases/latest manually."
    exit 1
fi

echo "Downloading $DOWNLOAD_URL..."
# Create a temp dir to work in
WORK_DIR=$(mktemp -d)
cd "$WORK_DIR"

# Download and pipe to tar
curl -sL "$DOWNLOAD_URL" | tar xz

# Directory name is static as per release.yml
EXTRACTED_DIR="plasmazones-linux-x86_64"

if [ ! -d "$EXTRACTED_DIR" ]; then
    echo "Error: Extraction failed or unexpected directory structure."
    ls -la
    exit 1
fi

echo "Starting installer..."
cd "$EXTRACTED_DIR"
./install.sh "$@"

# Cleanup is hard because we might be running FROM the temp dir if piped to bash
# But install.sh installs to ~/.local, so the source dir isn't needed after install.
echo "Cleaning up..."
rm -rf "$WORK_DIR"
