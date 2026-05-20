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

// Tie the scroll engine's standalone fallback constants (used only when no
// IScrollSettings is wired — engine unit tests with no FakeScrollSettings) to
// ConfigDefaults so a drift between the two values fails the build instead
// of silently diverging behaviour between production and tests. Both
// ScrollScreenState.h (for the engine constants) and configdefaults.h (for
// the schema bounds and defaults) are included above this TU, so this is the
// single shared reach-point that sees both layers.
static_assert(::PhosphorScrollEngine::kMinSizeFraction == ConfigDefaults::scrollColumnWidthMin(),
              "PhosphorScrollEngine::kMinSizeFraction drifted from ConfigDefaults::scrollColumnWidthMin");
static_assert(::PhosphorScrollEngine::kMaxSizeFraction == ConfigDefaults::scrollColumnWidthMax(),
              "PhosphorScrollEngine::kMaxSizeFraction drifted from ConfigDefaults::scrollColumnWidthMax");
static_assert(::PhosphorScrollEngine::kMinStripGap == ConfigDefaults::scrollInnerGapMin(),
              "PhosphorScrollEngine::kMinStripGap drifted from ConfigDefaults::scrollInnerGapMin");
static_assert(::PhosphorScrollEngine::kMaxStripGap == ConfigDefaults::scrollInnerGapMax(),
              "PhosphorScrollEngine::kMaxStripGap drifted from ConfigDefaults::scrollInnerGapMax");
static_assert(::PhosphorScrollEngine::kDefaultColumnWidthFraction == ConfigDefaults::scrollDefaultColumnWidth(),
              "PhosphorScrollEngine::kDefaultColumnWidthFraction drifted from "
              "ConfigDefaults::scrollDefaultColumnWidth");
static_assert(::PhosphorScrollEngine::kDefaultStripGap == ConfigDefaults::scrollInnerGap(),
              "PhosphorScrollEngine::kDefaultStripGap drifted from ConfigDefaults::scrollInnerGap");

PhosphorScrollEngine::ScrollEngine* Daemon::scrollEngine() const
{
    // Cached at engine-creation time in start.cpp; nulled in stop() before
    // m_scrollEngine.reset(). Avoids RTTI on the hot path
    // (onScrollPlacementChanged + applyPerScreenScrollOverrides).
    return m_scrollEngineCached;
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

    const QSet<QString> previousActive = m_scrollEngine->activeScreens();
    const bool screensChanged = (previousActive != scrollScreens);
    if (screensChanged) {
        m_scrollEngine->setActiveScreens(scrollScreens);
        // Invalidate the per-screen geometry cache for any screen leaving
        // scroll mode while staying connected. Without this, a stale
        // last-pushed-geometry entry sits in the cache until the screen rejoins
        // scroll mode and the dedup check happens to mismatch — wasted memory
        // for the duration. screenRemoved already handles the unplug case;
        // this handles the same-screen mode-switch case symmetrically.
        for (const QString& screenId : previousActive) {
            if (!scrollScreens.contains(screenId)) {
                m_lastScrollGeometryByScreen.remove(screenId);
            }
        }
    }
    // A screen entering scroll mode needs its per-screen overrides in the
    // engine before its windows resolve geometry. Push regardless of whether
    // the active set itself changed — a same-set settings edit (e.g. an
    // override appearing for a screen that's already active) still needs the
    // override pushed.
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
        // The screen disappeared between the placementChanged emit and this
        // resolve (a hot-unplug mid-flight). Silent skip is correct, but log
        // the miss so a debugging session can see why a strip stopped pushing
        // geometry even though the engine still thinks the screen is active.
        qCDebug(lcDaemon) << "scroll geometry resolve: screen has no working area" << screenId;
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
        // No tiled windows on this screen: invalidate the cache so a
        // subsequent placement that re-introduces the same windows at the
        // same rects is not falsely matched against a stale cache.
        m_lastScrollGeometryByScreen.remove(screenId);
        return;
    }

    // Dedupe against the last push for this screen. Slider drags at the
    // settings page (and the daemon's own ratchet of placementChanged →
    // applyPerScreenScrollOverrides → refresh → onScrollPlacementChanged)
    // can produce multiple resolves per tick that all yield identical pixel
    // geometry; emitting applyGeometriesBatch each time is wasted work for
    // the effect (full window-map walk + per-window moveResize). Compare
    // the windowId→QRect map directly — QHash::operator== is structural.
    QHash<QString, QRect> snapshot;
    snapshot.reserve(geometries.size());
    for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
        snapshot.insert(it.key(), it.value().toRect());
    }
    if (m_lastScrollGeometryByScreen.value(screenId) == snapshot) {
        return;
    }
    m_lastScrollGeometryByScreen.insert(screenId, snapshot);

    PhosphorProtocol::WindowGeometryList batch;
    batch.reserve(snapshot.size());
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        batch.append(PhosphorProtocol::WindowGeometryEntry::fromRect(it.key(), it.value(), screenId));
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

void Daemon::requestScrollConfigRefresh()
{
    // Coalesce: a settings-page slider drag fires the change signal at ~30 Hz,
    // and every emit re-resolves and re-pushes geometry for every active
    // scroll strip. Funnel through a single-shot 0-ms timer so the whole
    // burst collapses to one refresh in the next event-loop tick. The timer
    // is constructed in start.cpp (mirrors the animation-publish path) and
    // its slot calls refreshScrollConfigFromSettings exactly once.
    if (!m_scrollRefreshTimer.isActive()) {
        m_scrollRefreshTimer.start();
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
        // not restore an obsolete layout. QFile::remove returns false when
        // the file doesn't exist (the common case after first launch) AND
        // when the remove genuinely fails — log only the failure case so a
        // read-only directory or a race with another daemon instance shows
        // up in logs. exists() is a cheap stat — pre-checking avoids the
        // false-positive "removed nothing" warning for first launches.
        if (QFile::exists(path) && !QFile::remove(path)) {
            qCWarning(lcDaemon) << "Failed to remove stale scroll state at" << path;
        }
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
