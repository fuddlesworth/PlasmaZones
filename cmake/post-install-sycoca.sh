#!/bin/sh
# Refresh KDE sycoca cache after install. When run via sudo, refresh the
# original user's cache (root's cache doesn't help). Called by CMake install(CODE)
# and Makefile post-install.
# SPDX-License-Identifier: GPL-3.0-or-later

if [ -n "$SUDO_USER" ]; then
    su - "$SUDO_USER" -c "kbuildsycoca6 --noincremental" 2>/dev/null || true
else
    kbuildsycoca6 --noincremental 2>/dev/null || true
fi

echo ""
echo "Note: The KWin effect is enabled by default, but KWin must be"
echo "restarted to load it. Log out and back in, or run:"
echo "  kwin_wayland --replace &"
echo ""
