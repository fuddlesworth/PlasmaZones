// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"

#include "../../core/logging.h"
#include "../../dbus/scrolladaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../config/configdefaults.h"
#include "../config/settings.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PlasmaZones {

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

    QSet<QString> scrollScreens;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Scroll, screenId, desktop, activity)) {
            continue;
        }
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (PhosphorLayout::LayoutId::isScroll(assignmentId)) {
            scrollScreens.insert(screenId);
        }
    }

    const bool screensChanged = (m_scrollEngine->activeScreens() != scrollScreens);
    m_scrollEngine->setActiveScreens(scrollScreens);
    // A screen entering scroll mode needs its per-screen overrides in the
    // engine before its windows resolve geometry.
    applyPerScreenScrollOverrides();
    if (screensChanged && m_scrollAdaptor) {
        // Tell the KWin effect which screens are scroll-mode so it reports
        // their windows to the org.plasmazones.Scroll interface. The payload
        // is sourced from ScrollAdaptor::scrollScreens() — the same accessor
        // that backs the scrollScreens property — so the signal and a
        // subsequent property read cannot disagree. It is sorted there, since
        // QSet iteration order is unspecified.
        Q_EMIT m_scrollAdaptor->scrollScreensChanged(m_scrollAdaptor->scrollScreens());
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
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
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
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    if (!scroll) {
        return;
    }

    // Coerce a persisted QVariantList of fractions into the engine's typed
    // QVector<qreal>. Non-numeric junk is already dropped by the schema's
    // clampFractionList validator, so a plain toReal() per entry suffices.
    const auto toFractions = [](const QVariantList& list) {
        QVector<qreal> out;
        out.reserve(list.size());
        for (const QVariant& v : list) {
            out.append(v.toReal());
        }
        return out;
    };

    // Global defaults. Per-screen overrides layer on top via the engine's
    // effective*() accessors — see applyPerScreenScrollOverrides() below.
    scroll->setPresetColumnWidths(toFractions(m_settings->scrollPresetColumnWidths()));
    scroll->setPresetWindowHeights(toFractions(m_settings->scrollPresetWindowHeights()));
    scroll->setDefaultColumnWidth(m_settings->scrollDefaultColumnWidth());
    scroll->setInnerGap(m_settings->scrollInnerGap());
    scroll->setOuterGap(m_settings->scrollOuterGap());
    scroll->setViewportMode(m_settings->scrollCenterFocusedColumn() ? PhosphorScrollEngine::ScrollViewportMode::Centered
                                                                    : PhosphorScrollEngine::ScrollViewportMode::Fit);

    applyPerScreenScrollOverrides();

    // Re-resolve every active scroll strip so a gap / preset / centering change
    // surfaces immediately. onScrollPlacementChanged reads the just-updated
    // engine config (global + per-screen) when it builds the layout config.
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
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    if (!scroll) {
        return;
    }
    // Push each active scroll screen's per-screen override map into the engine
    // (mirrors updateAutotileScreens' per-screen autotile push). The engine's
    // effective*() accessors then resolve override → global per screen.
    const QSet<QString> screens = m_scrollEngine->activeScreens();
    for (const QString& screenId : screens) {
        const QVariantMap overrides = m_settings->getPerScreenScrollSettings(screenId);
        if (overrides.isEmpty()) {
            scroll->clearPerScreenConfig(screenId);
        } else {
            scroll->applyPerScreenConfig(screenId, overrides);
        }
    }
}

void Daemon::saveScrollState()
{
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    if (!scroll) {
        return;
    }
    const QString path = ConfigDefaults::scrollStateFilePath();
    const QJsonObject state = scroll->serializeEngineState();
    if (state.value(QLatin1String("states")).toArray().isEmpty()) {
        // No strips to persist — drop any stale file so a later restart does
        // not restore an obsolete layout.
        QFile::remove(path);
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcDaemon) << "Failed to write scroll state to" << path;
        return;
    }
    file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void Daemon::loadScrollState()
{
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
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
