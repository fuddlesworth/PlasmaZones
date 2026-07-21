// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Page-class predicates and shared-domain config key lists, used by BOTH the
// dirty-tracking surface (settingscontroller_pagestate.cpp) and the per-page
// Reset/Discard machinery (settingscontroller_pagereset.cpp).
//
// Internal to the settings app: not installed, not part of any public API.
// These were an anonymous namespace in _pagestate.cpp before that file was
// split at the 1150-line ceiling; internal linkage cannot span two translation
// units, so they are promoted here rather than duplicated. One definition of
// "which pages are animation pages" is what keeps the dirty check and the
// reset from ever disagreeing about a page's class.

#include "../config/settings.h"

#include <QString>

namespace PlasmaZones {

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
