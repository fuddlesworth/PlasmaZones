// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Paper",
    builtinId: "paper",
    description: "Equal-width overlapping pages like a document viewer; splitRatio controls page width",
    producesOverlappingZones: true,
    supportsMasterCount: false,
    supportsSplitRatio: true,
    defaultSplitRatio: 0.8,
    defaultMaxWindows: 6,
    minimumWindows: 1,
    zoneNumberDisplay: "last",
    supportsMemory: false
};

/**
 * Paper layout: each window is an equal-width "page" (default 80%
 * of screen width) distributed evenly across the screen. Windows
 * overlap like pages in a document viewer.
 *
 * splitRatio controls the page width as a fraction of the screen
 * width (0.8 = each page is 80% of screen width).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    // Overlapping layout — innerGap intentionally ignored (pages overlap by design)
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    const pageRatio = clampSplitRatio(params.splitRatio);
    let pageWidth = Math.floor(area.width * pageRatio);
    if (pageWidth < 1) pageWidth = 1;
    if (pageWidth > area.width) pageWidth = area.width;

    // Distribute pages evenly across the remaining space.
    // The leftover space is shared as offsets between pages.
    const leftover = area.width - pageWidth;
    // Clamp step so the last page doesn't overflow the area
    let step;
    if (count > 1) {
        step = Math.floor(leftover / (count - 1));
    } else {
        step = 0;
    }

    const zones = [];
    for (let i = 0; i < count; i++) {
        const zoneX = Math.min(area.x + i * step, area.x + area.width - 1);
        const zoneW = Math.max(1, Math.min(pageWidth, area.x + area.width - zoneX));
        zones.push({
            x: zoneX,
            y: area.y,
            width: zoneW,
            height: area.height
        });
    }

    return zones;
}
