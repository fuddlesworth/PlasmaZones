// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PhosphorControl {
class SearchController;
}

namespace PlasmaZones {

/**
 * @brief Seed the global search index with PlasmaZones-specific metadata that
 *        cannot be derived from the page registry.
 *
 * Page entries come free from the registry (title + breadcrumb); this adds
 * per-page synonyms (so "opacity" finds an "Appearance" page) and addressable
 * section / setting anchors (so "rendering backend" lands on that row and
 * reveals it). Dynamic content (rules, shaders, …) is wired separately via
 * ISearchProvider. This catalog grows as more pages are tagged.
 */
void seedSearchCatalog(PhosphorControl::SearchController* search);

} // namespace PlasmaZones
