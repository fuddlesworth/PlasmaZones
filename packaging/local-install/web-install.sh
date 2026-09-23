#!/bin/bash
# SPDX-FileCopyrightText: 2026 abirkel <abirkel@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-or-later
# PlasmaZones Web Installer
# Fetches the latest portable tarball release and installs it.

set -e

REPO="fuddlesworth/PlasmaZones"
ARTIFACT_PATTERN="linux-x86_64.tar.gz"

echo "Finding latest release for $REPO..."
API_URL="https://api.github.com/repos/$REPO/releases/latest"

# Fetch first, parse second. As a single pipeline the assignment took jq's
# (or cut's) exit status, and both exit 0 on empty input, so a failed fetch
# -- an unauthenticated GitHub API 403 is 60 requests/hour per IP -- fell
# through to the "artifact not found" message below and sent the user to
# look at a release that is perfectly fine.
if ! API_JSON=$(curl -fSs "$API_URL"); then
    echo "Error: could not reach the GitHub API at $API_URL."
    echo "If this persists you may be rate-limited; try again shortly."
    exit 1
fi

# Try jq first, fall back to grep/cut for systems without jq
if command -v jq >/dev/null 2>&1; then
    DOWNLOAD_URL=$(printf '%s' "$API_JSON" | jq -r ".assets[] | select(.name | contains(\"$ARTIFACT_PATTERN\")) | .browser_download_url" | head -n 1)
else
    DOWNLOAD_URL=$(printf '%s' "$API_JSON" | grep -o '"browser_download_url":[[:space:]]*"[^"]*'"$ARTIFACT_PATTERN"'"' | head -1 | cut -d'"' -f4)
fi

if [ -z "$DOWNLOAD_URL" ]; then
    echo "Error: Could not find latest release artifact matching '$ARTIFACT_PATTERN'."
    echo "Please check https://github.com/$REPO/releases/latest manually."
    exit 1
fi

echo "Downloading $DOWNLOAD_URL..."
WORK_DIR=$(mktemp -d)

# Download to a file so we can detect errors (piping curl|tar hides failures)
curl -fSL "$DOWNLOAD_URL" -o "$WORK_DIR/plasmazones.tar.gz"
tar xzf "$WORK_DIR/plasmazones.tar.gz" -C "$WORK_DIR"

# Directory name is static as per release.yml
EXTRACTED_DIR="$WORK_DIR/plasmazones-linux-x86_64"

if [ ! -d "$EXTRACTED_DIR" ]; then
    echo "Error: Extraction failed or unexpected directory structure."
    ls -la "$WORK_DIR"
    rm -rf "$WORK_DIR"
    exit 1
fi

echo "Starting installer..."
"$EXTRACTED_DIR/install.sh" "$@"

# Clean up temp directory
echo "Cleaning up..."
rm -rf "$WORK_DIR"
