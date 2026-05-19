// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"

#include "../../core/logging.h"
#include "../../dbus/scrolladaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../config/configdefaults.h"
#include "../../config/settings.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollLayout.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRect>
#include <QRectF>
#include <QSaveFile>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PlasmaZones {

PhosphorScrollEngine::ScrollEngine* Daemon::scrollEngine() const
{
    return dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
}

void Daemon::updateScrollScreens()
{
    if (!m_scrollEngine || !m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = currentDesktop();
    const QString activity = currentActivity();

    // Align the engine's per-context key with the daemon's current
    // desktop/activity before resolving — mirrors the setCurrentDesktop /
    // setCurrentActivity-before-update pattern used for autotile.
    m_scrollEngine->setCurrentDesktop(desktop);
    m_scrollEngine->setCurrentActivity(activity);

    // Master gate: when scrolling mode is globally disabled the active set
    // stays empty, so no strip resolves on any screen. Restored session state
    // in ScrollEngine is kept but dormant — re-enabling repopulates the set.
    QSet<QString> scrollScreens;
    if (m_settings && m_settings->scrollingEnabled()) {
        const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
        for (const QString& screenId : effectiveIds) {
            if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Scroll, screenId, desktop,
                                  activity)) {
                continue;
            }
            const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
            if (PhosphorLayout::LayoutId::isScroll(assignmentId)) {
                scrollScreens.insert(screenId);
            }
        }
    }

    const bool screensChanged = (m_scrollEngine->activeScreens() != scrollScreens);
    m_scrollEngine->setActiveScreens(scrollScreens);
    // A screen entering scroll mode needs its per-screen overrides in the
    // engine before its windows resolve geometry.
    applyPerScreenScrollOverrides();
    if (screensChanged) {
        // Resolve every active scroll strip now. setActiveScreens() emits no
        // placementChanged, and a restored window's windowOpened no-ops
        // (already tracked), so a strip restored from scroll-session.json — or
        // a screen newly entering scroll mode — would otherwise never be
        // pushed to the effect. onScrollPlacementChanged no-ops for a screen
        // with no strip yet.
        for (const QString& screenId : scrollScreens) {
            onScrollPlacementChanged(screenId);
        }
        if (m_scrollAdaptor) {
            // Tell the KWin effect which screens are scroll-mode so it reports
            // their windows to the org.plasmazones.Scroll interface. The
            // payload is sourced from ScrollAdaptor::scrollScreens() — the same
            // accessor that backs the scrollScreens property — so the signal
            // and a subsequent property read cannot disagree.
            Q_EMIT m_scrollAdaptor->scrollScreensChanged(m_scrollAdaptor->scrollScreens());
        }
    }
    qCDebug(lcDaemon) << "Updated scroll screens=" << scrollScreens;
}

void Daemon::onScrollPlacementChanged(const QString& screenId)
{
    if (screenId.isEmpty() || !m_scrollEngine || !m_screenManager || !m_windowTrackingAdaptor) {
        return;
    }
    // ScrollEngine is geometry-agnostic — it stores the strip; the daemon
    // resolves it to pixels because only the daemon knows the working area.
    auto* scroll = scrollEngine();
    if (!scroll) {
        return;
    }
    auto* state = dynamic_cast<PhosphorScrollEngine::ScrollScreenState*>(scroll->stateForScreen(screenId));
    if (!state) {
        return;
    }

    // Panel-excluded working area, virtual-screen aware — mirrors
    // AutotileEngine::screenGeometry().
    QRect workArea;
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        workArea = m_screenManager->screenAvailableGeometry(screenId);
    } else if (QScreen* screen = Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId)) {
        workArea = m_screenManager->actualAvailableGeometry(screen);
    }
    if (!workArea.isValid()) {
        return;
    }

    PhosphorScrollEngine::ScrollLayoutConfig config;
    // Gaps (logical px): the outer gap insets the strip from the working-area
    // edges; the inner gap separates adjacent columns and the tiles within a
    // column. The engine resolves each value as a per-screen override over the
    // global default — see ScrollEngine::effective*().
    config.outerGap = scroll->effectiveOuterGap(screenId);
    config.innerGap = scroll->effectiveInnerGap(screenId);
    config.presetWindowHeights = scroll->effectivePresetWindowHeights(screenId);
    config.viewportMode = scroll->effectiveViewportMode(screenId);

    // Column metrics are scroll-independent — resolve them once and feed the
    // same value to both the viewport computation and the geometry resolve.
    const PhosphorScrollEngine::ScrollColumnMetrics metrics =
        PhosphorScrollEngine::resolveColumnMetrics(*state, QRectF(workArea), config);

    // Scroll the strip so the focused column is on-screen, then resolve. The
    // viewport is geometry-dependent (it needs the working area), so the
    // daemon owns its computation; the engine only stores the result.
    state->setScrollX(PhosphorScrollEngine::computeViewportScroll(*state, QRectF(workArea), config, &metrics));

    const QHash<QString, QRectF> geometries =
        PhosphorScrollEngine::resolveScrollLayout(*state, QRectF(workArea), config, &metrics);
    if (geometries.isEmpty()) {
        return;
    }

    PhosphorProtocol::WindowGeometryList batch;
    batch.reserve(geometries.size());
    for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
        batch.append(PhosphorProtocol::WindowGeometryEntry::fromRect(it.key(), it.value().toRect(), screenId));
    }
    Q_EMIT m_windowTrackingAdaptor->applyGeometriesBatch(batch, QStringLiteral("scroll"));
}

void Daemon::refreshScrollConfigFromSettings()
{
    if (!m_scrollEngine || !m_settings) {
        return;
    }
    auto* scroll = scrollEngine();
    if (!scroll) {
        return;
    }

    // Global scroll geometry config is no longer pushed scalar-by-scalar: the
    // engine pulls it through PhosphorEngine::IScrollSettings (Settings
    // implements that interface and was wired into the engine at construction
    // via setEngineSettings). Per-screen overrides still layer on top via the
    // engine's effective*() accessors — see applyPerScreenScrollOverrides().
    applyPerScreenScrollOverrides();

    // Re-resolve every active scroll strip so a gap / preset / centering change
    // surfaces immediately. onScrollPlacementChanged reads the just-updated
    // engine config (global + per-screen) when it builds the layout config.
    // No re-gate on scrollingEnabled is needed here: updateScrollScreens has
    // already emptied activeScreens() when scrolling is disabled, so the
    // re-resolve loop below no-ops.
    const QSet<QString> screens = m_scrollEngine->activeScreens();
    for (const QString& screenId : screens) {
        onScrollPlacementChanged(screenId);
    }
}

void Daemon::applyPerScreenScrollOverrides()
{
    if (!m_scrollEngine || !m_settings) {
        return;
    }
    auto* scroll = scrollEngine();
    if (!scroll) {
        return;
    }
    // Push each active scroll screen's per-screen override map into the engine
    // (mirrors updateAutotileScreens' per-screen autotile push). The engine's
    // effective*() accessors then resolve override → global per screen.
    const QSet<QString> screens = m_scrollEngine->activeScreens();
    for (const QString& screenId : screens) {
        const QVariantMap overrides = m_settings->getPerScreenScrollSettings(screenId);
        // Compare against the engine's currently-applied overrides and skip the
        // push when nothing changed — applyPerScreenConfig/clearPerScreenConfig
        // schedule a deferred re-resolve, and refreshScrollConfigFromSettings()
        // calls this on every scroll-setting edit. Mirrors updateAutotileScreens.
        if (overrides == scroll->perScreenOverrides(screenId)) {
            continue;
        }
        if (overrides.isEmpty()) {
            scroll->clearPerScreenConfig(screenId);
        } else {
            scroll->applyPerScreenConfig(screenId, overrides);
        }
    }
}

void Daemon::saveScrollState()
{
    auto* scroll = scrollEngine();
    if (!scroll) {
        return;
    }
    const QString path = ConfigDefaults::scrollStateFilePath();
    if (!scroll->hasPersistableState()) {
        // No strips to persist — drop any stale file so a later restart does
        // not restore an obsolete layout.
        QFile::remove(path);
        return;
    }
    const QJsonObject state = scroll->serializeEngineState();
    // QSaveFile commits atomically (write to a temp file, then rename), so a
    // crash mid-write cannot leave a truncated scroll-session.json behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcDaemon) << "Failed to write scroll state to" << path;
        return;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qCWarning(lcDaemon) << "Failed to commit scroll state to" << path;
        return;
    }
    qCDebug(lcDaemon) << "Saved scroll state to" << path;
}

void Daemon::loadScrollState()
{
    auto* scroll = scrollEngine();
    if (!scroll) {
        return;
    }
    QFile file(ConfigDefaults::scrollStateFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcDaemon) << "Ignoring malformed scroll state:" << err.errorString();
        return;
    }
    scroll->deserializeEngineState(doc.object());
    qCDebug(lcDaemon) << "Restored scroll state from" << file.fileName();
}

} // namespace PlasmaZones
