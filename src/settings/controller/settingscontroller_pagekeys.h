// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Page-class predicates, shared-domain config key lists, the quick-layout slot
// count, and the loading-flag scope guard shared across the settings app's page
// translation units.
//
// Internal to the settings app: not installed, not part of any public API.
// These were an anonymous namespace in _pagestate.cpp before that file was
// split at the 1150-line ceiling. Internal linkage cannot span translation
// units, so anything more than one settings-app TU needs lives here rather
// than being duplicated. One definition of "which pages are animation pages"
// is what keeps the dirty check and the reset from ever disagreeing about a
// page's class. Consumers vary per entity — each declaration below names its
// own, rather than the file claiming a single pair.

#include "config/settings.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QString>
#include <QtGlobal>

namespace PlasmaZones {

/// Raises a bool flag for the enclosing scope and restores the PREVIOUS value
/// on exit (not a hard clear), so nesting is safe and an exception escaping the
/// scope cannot strand the flag raised.
///
/// Two flags use it. Raising SettingsController::m_loading suppresses
/// onSettingsPropertyChanged, so a reset/discard's own writes do not mark the
/// page dirty again; raising m_settingActivePage refuses a reentrant
/// setActivePage. Both wrap a body that synchronously runs arbitrary QML page
/// construction, which is exactly why the restore has to be unconditional.
///
/// DEFINED HERE, not in a .cpp: it is constructed from three TUs
/// (settingscontroller_pagestate.cpp, settingscontroller_pagereset.cpp and
/// settingscontroller_lifecycle.cpp), and a TU-local copy in one of them only
/// ever linked because CMAKE_UNITY_BUILD merged the files into one batch. Same
/// rationale as SettingsController::DirtyEmitScope.
class ScopedFlag
{
public:
    explicit ScopedFlag(bool& flag)
        : m_flag(flag)
        , m_previous(flag)
    {
        flag = true;
    }
    ~ScopedFlag()
    {
        release();
    }
    /// Restore the flag NOW instead of at end of scope, for a caller whose last
    /// step has to run with the flag already down (defaults() recomputes the
    /// dirty set outside the gate it held over the reset itself). Idempotent:
    /// the destructor calls this too, and a second call is a no-op rather than
    /// a second restore.
    void release()
    {
        if (m_active) {
            m_active = false;
            m_flag = m_previous;
        }
    }
    Q_DISABLE_COPY_MOVE(ScopedFlag)

private:
    bool& m_flag;
    bool m_previous;
    bool m_active = true;
};

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

/// The three Quick Shortcuts pages. Same shared-classification rationale.
bool isShortcutsPage(const QString& page);

/// The wire values the daemon's quick-layout slot map is keyed on. Named
/// aliases of AssignmentEntry::Mode so the reset loop, the per-slot accessors
/// and the Save-time flush all spell the same mode the same way instead of
/// carrying bare 0/1/2 literals with a comment each. The enum's numbering is
/// frozen (see the AssignmentEntry::Mode doc), so these are stable on the wire.
/// PascalCase rather than UPPER_SNAKE on purpose, matching the other named
/// constants in this header and the repo's wider precedent (ReasonDaemonUnreachable).
constexpr int QuickSlotModeSnapping = static_cast<int>(PhosphorZones::AssignmentEntry::Snapping);
constexpr int QuickSlotModeTiling = static_cast<int>(PhosphorZones::AssignmentEntry::Autotile);
constexpr int QuickSlotModeScrolling = static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling);

/// Quick-layout slots are numbered 1..QUICK_LAYOUT_SLOT_COUNT. Shared by the
/// slot accessors' bounds checks (settingscontroller_session.cpp) and the
/// Quick Shortcuts reset loop (settingscontroller_pagereset.cpp), so the bound
/// and the loop that walks it cannot drift apart. Aliases the protocol-level
/// constant the daemon validates against (layoutadaptor.cpp), so the two trees
/// cannot disagree about how many slots exist either.
constexpr int QUICK_LAYOUT_SLOT_COUNT = PhosphorProtocol::Service::QuickLayoutSlotCount;

/// The three per-mode library pages (snapping-layouts / tiling-library /
/// scrolling-templates). They browse and manage separate stores that write
/// immediately, so they carry no staged config state of their own and are
/// never dirty; the one config write their context menu performs (set the
/// family's default) is attributed to the page that owns the key through an
/// external-edit envelope. Consumers: isPageDirty's never-dirty early return,
/// dirtyScopeFor's hoist count (settingscontroller_pagetopology.cpp), and —
/// through the SettingsController::isLibraryPage Q_INVOKABLE bridge — the
/// window-scoped layoutOperationFailed fallback in Main.qml.
bool isLibraryPage(const QString& page);

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
