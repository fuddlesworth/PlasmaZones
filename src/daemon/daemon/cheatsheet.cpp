// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — shortcut cheatsheet overlay
//
// Split out of daemon/osd.cpp, which had grown past the file-size ceiling.
// The sheet is its own overlay surface with its own Escape grab and its own
// per-screen push state, so it shares nothing with the OSD cards beyond the
// Daemon members both read.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "daemon/overlayservice.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "helpers.h"
#include "phosphor_i18n.h"

#include <PhosphorZones/AssignmentEntry.h>

#include <QKeySequence>

namespace PlasmaZones {

namespace {

// Dedicated Escape ad-hoc grab id — deliberately NOT the shared
// kCancelOverlayId: reusing that would drag the cheatsheet into the
// cancelSnap precedence chain and its cross-consumer release guard.
// KGlobalAccel routes one action per key, so the daemon keeps at most one
// Escape consumer active by dismissing sibling modals around show.
const QLatin1String kCheatsheetDismissId("cheatsheet_dismiss");

// String form of the per-screen tiling mode as CheatsheetContent consumes
// it. All three engines are live; the sheet filters each mode's group plus
// the mode-independent ones.
QString cheatsheetModeString(PhosphorZones::AssignmentEntry::Mode mode)
{
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        return QStringLiteral("autotile");
    case PhosphorZones::AssignmentEntry::Scrolling:
        return QStringLiteral("scrolling");
    case PhosphorZones::AssignmentEntry::Snapping:
        break;
    }
    return QStringLiteral("snapping");
}

} // namespace

void Daemon::toggleCheatsheet()
{
    if (!m_overlayService || !m_shortcutManager) {
        return;
    }
    if (m_overlayService->isCheatsheetVisible()) {
        m_overlayService->hideCheatsheet();
        return;
    }
    showCheatsheetOnCursorScreen();
}

void Daemon::showCheatsheetOnCursorScreen()
{
    if (!m_overlayService || !m_shortcutManager) {
        return;
    }
    // No cheatsheet during an interactive drag: the kwin-effect holds a
    // keyboard grab for the drag's lifetime and routes Escape to cancelSnap
    // itself (grabbedKeyboardEvent, kwin-effect/plasmazoneseffect.cpp), so
    // the dismiss grab bound
    // below would never fire, and the sheet would also overlap the live
    // drag surfaces. The user can re-press after dropping the window.
    if (m_windowDragAdaptor && m_windowDragAdaptor->isDragInFlight()) {
        qCDebug(lcDaemon) << "Cheatsheet: suppressed during interactive drag";
        return;
    }

    // Screen-targeted like the picker and the mode toggle: the user's
    // intent is "the screen I am looking at", so resolve cursor-first.
    const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCDebug(lcDaemon) << "Cheatsheet: no screen info";
        return;
    }

    // At most one Escape-consuming modal at a time (see kCheatsheetDismissId
    // note): dismiss the picker / snap assist first. Their dismissed signals
    // release the shared cancel-overlay Escape grab synchronously, so the
    // cheatsheet's own Escape registration below cannot be silently no-op'd
    // by a key-level conflict.
    if (m_overlayService->isLayoutPickerVisible()) {
        m_overlayService->hideLayoutPicker();
    }
    if (m_overlayService->isSnapAssistVisible()) {
        m_overlayService->hideSnapAssist();
    }

    // With every mode switched off the context is modeless: there is no mode
    // whose shortcuts would be truthful, so say that rather than draw a sheet
    // filtered by a mode nothing is running in.
    if (!anyModeEnabled()) {
        showModelessOsd(screenId);
        return;
    }

    const CheatsheetPushState push = cheatsheetPushStateFor(screenId);
    m_overlayService->showCheatsheet(screenId, m_shortcutManager->cheatsheetModel(), push.modeString,
                                     push.autotileAvailable, push.scrollingAvailable, push.layoutsAvailable,
                                     push.layoutsAreTemplates);

    // Bind Escape only on a successful show — showCheatsheet bails on
    // missing screen/shell/catalog, and the sheet's own dismiss path is the
    // intended releaser. (A grab bound on a failed show would in practice
    // still be released by the next picker or snap-assist open, whose
    // unconditional hideCheatsheet emits cheatsheetDismissed even for a
    // hidden sheet; the guard just avoids a grab with no sheet in the
    // meantime.)
    if (m_overlayService->isCheatsheetVisible()) {
        m_shortcutManager->registerAdhocShortcut(kCheatsheetDismissId, QKeySequence(Qt::Key_Escape),
                                                 PhosphorI18n::tr("Dismiss Shortcut Cheatsheet"), [this] {
                                                     if (m_overlayService) {
                                                         m_overlayService->hideCheatsheet();
                                                     }
                                                 });
    }
}

void Daemon::refreshCheatsheetIfVisible()
{
    if (!m_overlayService || !m_shortcutManager || !m_overlayService->isCheatsheetVisible()) {
        return;
    }
    // Re-resolve for the screen the sheet is BOUND to — never retarget to
    // the cursor's current screen; only mode changes on the bound screen
    // refilter (the sheet stays put like the picker does).
    const QString screenId = m_overlayService->cheatsheetScreenId();
    if (screenId.isEmpty()) {
        return;
    }
    // Same modeless condition as the show path. No OSD here: the user
    // pressed nothing, so an unprompted card would be noise.
    if (!anyModeEnabled()) {
        m_overlayService->hideCheatsheet();
        return;
    }
    const CheatsheetPushState push = cheatsheetPushStateFor(screenId);
    m_overlayService->refreshCheatsheet(m_shortcutManager->cheatsheetModel(), push.modeString, push.autotileAvailable,
                                        push.scrollingAvailable, push.layoutsAvailable, push.layoutsAreTemplates);
}

bool Daemon::modeEnabled(PhosphorZones::AssignmentEntry::Mode mode) const
{
    // The master switches, keyed by mode. Same mapping the mode toggle's
    // cycle uses to skip disabled modes (daemon/autotile_init.cpp); kept as
    // a Daemon member because the cheatsheet needs the identical question
    // and a second open-coded switch is one settings rename away from
    // disagreeing with the toggle.
    if (!m_settings) {
        return false;
    }
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Snapping:
        return m_settings->snappingEnabled();
    case PhosphorZones::AssignmentEntry::Autotile:
        return m_settings->autotileEnabled();
    case PhosphorZones::AssignmentEntry::Scrolling:
        return m_settings->scrollingEnabled();
    }
    return false;
}

bool Daemon::anyModeEnabled() const
{
    return modeEnabled(PhosphorZones::AssignmentEntry::Snapping)
        || modeEnabled(PhosphorZones::AssignmentEntry::Autotile)
        || modeEnabled(PhosphorZones::AssignmentEntry::Scrolling);
}

void Daemon::showModelessOsd(const QString& screenId)
{
    // Structural twin of showNotAssignedOsd (daemon/osd.cpp): same suppress
    // gate, same style branch, same preview-then-text fallback.
    if (shouldSuppressOsd(screenId)) {
        return;
    }
    const OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;
    if (style == OsdStyle::None) {
        return;
    }
    const QString text = PhosphorI18n::tr("No placement mode is turned on");
    if (style == OsdStyle::Preview && m_overlayService) {
        m_overlayService->showDisabledOsd(text, screenId);
        qCInfo(lcDaemon) << "Cheatsheet: modeless context, preview OSD screen=" << screenId;
        return;
    }
    showKdeTextOsd(QStringLiteral("dialog-cancel"), text);
    qCInfo(lcDaemon) << "Cheatsheet: modeless context, text OSD screen=" << screenId;
}

Daemon::CheatsheetPushState Daemon::cheatsheetPushStateFor(const QString& screenId) const
{
    // Single resolver for everything both cheatsheet push sites hand the
    // overlay: show and refresh MUST agree (a divergence shows up as a sheet
    // whose filter changes on refresh), so neither site open-codes the set.
    using Mode = PhosphorZones::AssignmentEntry::Mode;

    // ScreenModeRouter answers placement ROUTING, not feature enablement: its
    // catch-all is Snapping (screenmoderouter.cpp), returned even with
    // snapping switched off, and the "autotile:none" sentinel pins a screen to
    // Autotile regardless of the tiling switch. Taking that answer raw filtered
    // the sheet by a mode the user had turned off, which listed that mode's
    // dead keys and hid every other group. Resolve to an enabled mode, in the
    // same cycle order the mode toggle skips disabled modes in
    // (daemon/autotile_init.cpp). Callers gate on anyModeEnabled(), so the loop
    // always finds one.
    Mode mode = currentModeFor(screenId);
    if (!modeEnabled(mode)) {
        for (const Mode candidate : {Mode::Snapping, Mode::Autotile, Mode::Scrolling}) {
            if (modeEnabled(candidate)) {
                mode = candidate;
                break;
            }
        }
    }

    // Layout capability follows the mode the sheet is FILTERED by. On the
    // fallback path that differs from the routed one, and reading the routed
    // engine would tag scrolling's template rows with placement wording.
    const LayoutSupport support = layoutSupportForCheatsheetMode(mode, screenId);
    return {cheatsheetModeString(mode), m_settings && m_settings->autotileEnabled(),
            m_settings && m_settings->scrollingEnabled(), support != LayoutSupport::None,
            support == LayoutSupport::Templates};
}

PhosphorEngine::IPlacementEngine::LayoutSupport
Daemon::layoutSupportForCheatsheetMode(PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId) const
{
    // Routed mode: defer to the router-backed resolver so the common path
    // keeps its exact behaviour, shutdown-window fallback included.
    if (mode == currentModeFor(screenId)) {
        return layoutSupportForScreen(screenId);
    }
    const PhosphorEngine::PlacementEngineBase* engine = nullptr;
    switch (mode) {
    case PhosphorZones::AssignmentEntry::Autotile:
        engine = m_autotileEngine.get();
        break;
    case PhosphorZones::AssignmentEntry::Snapping:
        engine = m_snapEngine.get();
        break;
    case PhosphorZones::AssignmentEntry::Scrolling:
        engine = m_scrollEngine.get();
        break;
    }
    if (!engine) {
        return layoutSupportForScreen(screenId);
    }
    return engine->layoutSupport();
}

void Daemon::onCheatsheetDismissed()
{
    // Fires on EVERY dismissal path (toggle re-press, Escape, backdrop,
    // teardown) — the right place to release the Escape grab.
    if (m_shortcutManager) {
        m_shortcutManager->unregisterAdhocShortcut(kCheatsheetDismissId);
    }
}

} // namespace PlasmaZones
