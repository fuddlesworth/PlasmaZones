// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Focus + Sidebar
// @description Main window with vertically stacked sidebar; ratio controls main window width
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.7
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Focus + Sidebar layout: one large main window on the left with a narrow
 * sidebar column on the right containing vertically stacked small windows.
 * splitRatio controls the main window width fraction (default 0.7 = 70%).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    const area = params.area;
    const gap = params.innerGap || 0;

    if (count <= 1) {
        return [{ x: area.x, y: area.y, width: area.width, height: area.height }];
    }

    const splitRatio = params.splitRatio;
    const mainWidth = Math.max(1, Math.round(area.width * splitRatio - gap / 2));
    const sidebarX = Math.min(area.x + mainWidth + gap, area.x + area.width - 1);
    const sidebarWidth = Math.max(1, area.x + area.width - sidebarX);

    const zones = [];

    // Window 1: main window on the left
    zones.push({
        x: area.x,
        y: area.y,
        width: mainWidth,
        height: area.height
    });

    // Windows 2+: stack vertically in the right sidebar column
    const sidebarCount = count - 1;
    const totalGaps = (sidebarCount - 1) * gap;
    const windowHeight = Math.max(1, Math.round((area.height - totalGaps) / sidebarCount));

    for (let i = 0; i < sidebarCount; i++) {
        const y = area.y + i * (windowHeight + gap);
        const h = (i === sidebarCount - 1)
            ? Math.max(1, area.y + area.height - y)
            : windowHeight;

        zones.push({
            x: sidebarX,
            y: y,
            width: sidebarWidth,
            height: h
        });
    }

    return zones;
}
