#!/bin/bash
# SPDX-FileCopyrightText: 2026 abirkel <abirkel@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-or-later
# PlasmaZones Uninstaller
# Removes PlasmaZones installed via local-install tarball.

# Default install prefix
PREFIX="$HOME/.local"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
        PREFIX="$2"
        shift 2
        ;;
        --help)
        echo "Usage: ./uninstall.sh [--prefix PATH]"
        echo "Default prefix: $HOME/.local"
        exit 0
        ;;
        *)
        echo "Unknown option: $1"
        echo "Use --help for usage information"
        exit 1
        ;;
    esac
done

MANIFEST_FILE="$PREFIX/share/plasmazones/install_manifest.txt"

# Check if manifest exists
if [ ! -f "$MANIFEST_FILE" ]; then
    echo "Error: No installation manifest found at $MANIFEST_FILE"
    echo "Cannot safely uninstall without knowing which files to remove."
    echo ""
    echo "If you installed to a different prefix, run:"
    echo "  $0 --prefix /path/to/prefix"
    exit 1
fi

# Read version from manifest
VERSION=$(head -n 1 "$MANIFEST_FILE" | sed -n 's/^# version: //p')
if [ -n "$VERSION" ]; then
    echo "Found PlasmaZones v$VERSION installation at $PREFIX"
else
    echo "Found PlasmaZones installation at $PREFIX"
fi

# Count files to be removed
FILE_COUNT=$(grep -v '^#' "$MANIFEST_FILE" | grep -v '^$' | grep -v '^DIR: ' | wc -l)

echo ""
echo "This will uninstall PlasmaZones from: $PREFIX"
echo "Files to be removed: $FILE_COUNT"
echo ""
read -p "Proceed with uninstallation? [y/N] " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Uninstallation cancelled."
    exit 0
fi

# Stop and disable service
if command -v systemctl >/dev/null 2>&1; then
    if systemctl --user is-active --quiet plasmazones.service 2>/dev/null; then
        echo "Stopping plasmazones service..."
        systemctl --user stop plasmazones.service
    fi

    if systemctl --user is-enabled --quiet plasmazones.service 2>/dev/null; then
        echo "Disabling plasmazones service..."
        systemctl --user disable plasmazones.service
    fi

    # Remove service file from user config directory
    rm -f "$HOME/.config/systemd/user/plasmazones.service"

    # Reload systemd immediately after service removal
    systemctl --user daemon-reload 2>/dev/null || true
fi

# Always remove environment configuration (created unconditionally by installer)
rm -f "$HOME/.config/environment.d/10-plasmazones.conf"

# Ask about config removal upfront
echo ""
read -p "Remove user configuration and layouts? [y/N] " -n 1 -r
echo
REMOVE_CONFIG=false
if [[ $REPLY =~ ^[Yy]$ ]]; then
    REMOVE_CONFIG=true
    echo "Will remove configuration and layouts."
else
    echo "Will preserve configuration and layouts."
fi
echo ""

# Remove files from manifest (skip comment lines and directory entries)
echo "Removing installed files..."
REMOVED_COUNT=0
while IFS= read -r line; do
    # Skip comment lines, empty lines, and directory entries
    [[ "$line" =~ ^#.*$ ]] && continue
    [[ -z "$line" ]] && continue
    [[ "$line" =~ ^DIR:.*$ ]] && continue

    FILE_PATH="$PREFIX/$line"
    # Remove both regular files and symlinks
    if [ -f "$FILE_PATH" ] || [ -L "$FILE_PATH" ]; then
        rm -f "$FILE_PATH" || echo "Warning: Failed to remove $FILE_PATH"
        REMOVED_COUNT=$((REMOVED_COUNT + 1))
    fi
done < "$MANIFEST_FILE"

echo "Removed $REMOVED_COUNT files."

# Clean up directories tracked in manifest (in reverse order, deepest first)
echo "Cleaning up empty directories..."
grep '^DIR: ' "$MANIFEST_FILE" 2>/dev/null | sed 's/^DIR: //' | sort -r | while IFS= read -r dir; do
    DIR_PATH="$PREFIX/$dir"
    if [ -d "$DIR_PATH" ]; then
        rmdir "$DIR_PATH" 2>/dev/null || true  # Only removes if empty
    fi
done

# Remove config if user requested
if [ "$REMOVE_CONFIG" = true ]; then
    echo "Removing configuration and layouts..."
    rm -rf "$PREFIX/share/plasmazones"
    rm -f "$HOME/.config/plasmazonesrc"
    echo "Configuration removed."
else
    # Remove manifest and uninstall script but keep user data
    rm -f "$MANIFEST_FILE"
    rm -f "$PREFIX/share/plasmazones/uninstall.sh"
fi

# Refresh KDE cache
if command -v kbuildsycoca6 >/dev/null 2>&1; then
    echo "Refreshing KDE service cache..."
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo ""
echo "============================================"
echo "Uninstall complete!"
echo "============================================"
echo ""
echo "IMPORTANT: You should LOG OUT and LOG BACK IN to fully remove PlasmaZones."
echo "This ensures all environment variables are cleared from your session."
echo ""
