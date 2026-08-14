// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Internal helpers shared by the search-catalog seeding TUs
// (searchcatalog.cpp and searchcatalog_animations.cpp). Not installed and not
// part of any public surface — the public entry point stays searchcatalog.h.

#include <PhosphorControl/SearchController.h>
#include <PhosphorControl/SearchEntry.h>

#include <QString>
#include <QStringList>

namespace PlasmaZones {
namespace SearchCatalogDetail {

// `advancedOnly` mirrors the `advancedOnly:` declaration on the row or card
// this anchor points at, for anchors whose HOST PAGE shows in both modes. The
// registry's page tier cannot express a per-row tier; the flag classifies the
// entry AdvancedOnly, which badges the result and makes a simple-mode
// activation switch to advanced first — without it the activation would skip
// the switch and reveal a collapsed row. The two declarations are
// cross-checked by tests/unit/settings/test_search_catalog_tiers.cpp, so they
// cannot drift apart silently.
inline void addSetting(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                       const QString& title, const QStringList& keywords = {}, bool advancedOnly = false)
{
    PhosphorControl::SearchEntry e;
    e.kind = PhosphorControl::SearchEntry::Kind::Setting;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.keywords = keywords;
    e.advancedOnly = advancedOnly;
    search->addEntry(e);
}

inline void addSection(PhosphorControl::SearchController* search, const QString& pageId, const QString& anchor,
                       const QString& title, bool advancedOnly = false)
{
    PhosphorControl::SearchEntry e;
    e.kind = PhosphorControl::SearchEntry::Kind::Section;
    e.pageId = pageId;
    e.anchor = anchor;
    e.title = title;
    e.advancedOnly = advancedOnly;
    search->addEntry(e);
}

} // namespace SearchCatalogDetail

/// Seed the animation-event anchors (the reveal-tagged per-event rows on the
/// Animations pages). Split out of seedSearchCatalog by concern — the single
/// catalog TU had crossed the file-size ceiling. Called only by
/// seedSearchCatalog.
void seedAnimationEventAnchors(PhosphorControl::SearchController* search);

/// Seed the condensed simple-mode pages' anchors. Same split-by-concern as
/// the animation events above (searchcatalog_simple.cpp). Called only by
/// seedSearchCatalog.
void seedSimplePageAnchors(PhosphorControl::SearchController* search);

} // namespace PlasmaZones
