// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Page-class predicates and shared-domain config key lists, used by BOTH the
// dirty-tracking surface (settingscontroller_pagestate.cpp) and the per-page
// Reset/Discard machinery (settingscontroller_pagereset.cpp).
//
// Internal to the settings app: not installed, not part of any public API.
// These were an anonymous namespace in _pagestate.cpp before that file was
// split at the 1150-line ceiling. Internal linkage cannot span translation
// units, so anything more than one settings-app TU needs lives here rather
// than being duplicated. One definition of "which pages are animation pages"
// is what keeps the dirty check and the reset from ever disagreeing about a
// page's class. Consumers vary per entity — each declaration below names its
// own, rather than the file claiming a single pair.

#include "../config/settings.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include <QString>

namespace PlasmaZones {

/// Which drag-to-reorder page this is, or None. Returns the KIND rather than a
/// bool so the three consumers (dirty check, Reset, Discard) each dispatch on
/// an enumerator instead of testing one page and letting the else-branch stand
/// for "the other one". Under a bool, a third ordering page satisfied the test
/// and then silently read and reset the tiling order; here it is a new
/// enumerator every switch has to answer for.
enum class OrderingPageKind {
    None,
    Snapping,
    Tiling,
};
OrderingPageKind orderingPageKind(const QString& page);

/// The two Quick Shortcuts pages. Same shared-classification rationale.
bool isShortcutsPage(const QString& page);

/// Quick-layout slots are numbered 1..kQuickLayoutSlotCount. Shared by the
/// slot accessors' bounds checks (settingscontroller_session.cpp) and the
/// Quick Shortcuts reset loop (settingscontroller_pagereset.cpp), so the bound
/// and the loop that walks it cannot drift apart. Aliases the protocol-level
/// constant the daemon validates against (layoutadaptor.cpp), so the two trees
/// cannot disagree about how many slots exist either.
constexpr int kQuickLayoutSlotCount = PhosphorProtocol::Service::QuickLayoutSlotCount;

/// Every animation leaf shares one staging domain and one ShaderProfileTree
/// key, but Reset/Discard/dirty are scoped per leaf — see animationPageScope.
bool isAnimationPage(const QString& page);

/// Animation config keys owned by the General leaf alone.
const Settings::ConfigKeyList& animationGeneralConfigKeys();

/// Every animation config key, General's included.
const Settings::ConfigKeyList& animationConfigKeys();

/// Decoration surface + library leaves, which share one DecorationProfileTree
/// key and scope per surface root — see decorationPageScope.
bool isDecorationPage(const QString& page);

/// Decoration config keys shared across the decoration leaves.
const Settings::ConfigKeyList& decorationConfigKeys();

} // namespace PlasmaZones
