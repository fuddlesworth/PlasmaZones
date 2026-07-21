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
#include <QtGlobal>

namespace PlasmaZones {

/// Suppresses onSettingsPropertyChanged for the enclosing scope so a
/// reset/discard's resetKeys/discardKeys writes do not each mark the page
/// dirty again. Raises the flag on entry and restores the PREVIOUS value on
/// exit rather than hard-clearing, so nesting is safe. Every reset/discard
/// branch shares this instead of hand-rolling the save/set/qScopeGuard triple.
///
/// Header-scoped rather than file-local because the reset/discard branches now
/// live in a different translation unit from the dirty-tracking code.
class LoadingScope
{
public:
    explicit LoadingScope(bool& flag)
        : m_flag(flag)
        , m_previous(flag)
    {
        flag = true;
    }
    ~LoadingScope()
    {
        m_flag = m_previous;
    }
    Q_DISABLE_COPY_MOVE(LoadingScope)

private:
    bool& m_flag;
    bool m_previous;
};

/// The two drag-to-reorder pages, whose state is the staged order optional
/// rather than config-manifest keys.
bool isOrderingPage(const QString& page);

/// The two Quick Shortcuts pages, whose editable state is the per-mode staged
/// quick-slot layout assignments in StagingService.
bool isShortcutsPage(const QString& page);

/// Quick-layout slots are numbered 1..9 (see SettingsController::getQuickLayoutSlot).
constexpr int kQuickLayoutSlotCount = 9;

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
