// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "autotileengine/engine_internal.h"

namespace PhosphorTileEngine {

AutotileEngine::AutotileEngine(PhosphorZones::LayoutRegistry* layoutManager,
                               PhosphorEngine::IWindowTrackingService* windowTracker,
                               PhosphorScreens::ScreenManager* screenManager,
                               PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QObject* parent)
    : PlacementEngineBase(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_screenManager(screenManager)
    , m_algorithmRegistry(algorithmRegistry)
    , m_config(std::make_unique<AutotileConfig>())
    , m_configResolver(std::make_unique<PerScreenConfigResolver>(this))
    , m_navigation(std::make_unique<NavigationController>(this))
    , m_algorithmId(PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId())
{
    // In production (Daemon::start) all four dependencies are non-null.
    // Headless unit tests deliberately pass nullptr to construct an engine
    // with minimal parents for testing peripheral classes (adaptors, bridges,
    // sub-controllers) — every method that dereferences a dependency guards
    // it locally. Do not Q_ASSERT here.

    // Guard timer: while active, refreshConfigFromSettings() skips overwriting
    // splitRatio/masterCount with Settings values. Mirrors the old SettingsBridge
    // m_shortcutSaveTimer — restarts on each write-back so rapid shortcut presses
    // keep the guard alive until the burst settles.
    m_writeBackGuardTimer.setSingleShot(true);
    m_writeBackGuardTimer.setInterval(500);
    connect(&m_writeBackGuardTimer, &QTimer::timeout, this, &AutotileEngine::settingsPersistRequested);

    m_settingsRetileTimer.setSingleShot(true);
    m_settingsRetileTimer.setInterval(100);
    connect(&m_settingsRetileTimer, &QTimer::timeout, this, [this]() {
        if (isEnabled()) {
            m_pendingRetileScreens.clear();
            retile();
        }
    });

    // Bounded retry timer for transient screen geometry failures.
    // When QScreen is unavailable during desktop switch, retileScreen defers
    // to this timer rather than silently dropping the retile.
    m_retileRetryTimer.setSingleShot(true);
    m_retileRetryTimer.setInterval(RetileRetryIntervalMs);
    connect(&m_retileRetryTimer, &QTimer::timeout, this, &AutotileEngine::processRetileRetries);

    connectSignals();
}

AutotileEngine::~AutotileEngine() = default;

} // namespace PhosphorTileEngine
