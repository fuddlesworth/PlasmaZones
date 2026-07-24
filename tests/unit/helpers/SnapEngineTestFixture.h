// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTest>
#include <QSignalSpy>
#include <QLoggingCategory>

#include <memory>

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowPlacementStore.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorRules/MatchExpression.h>
#include <PhosphorRules/MatchTypes.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleSet.h>
#include "config/configbackends.h"
#include "core/interfaces/interfaces.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

// =========================================================================
// Minimal stubs for WTS constructor assertions
// =========================================================================

class StubZoneDetectorSnap : public PhosphorZones::IZoneDetector
{
    Q_OBJECT
public:
    explicit StubZoneDetectorSnap(QObject* parent = nullptr)
        : PhosphorZones::IZoneDetector(parent)
    {
    }
    PhosphorZones::Layout* layout() const override
    {
        return nullptr;
    }
    void setLayout(PhosphorZones::Layout*) override
    {
    }
    PhosphorZones::ZoneDetectionResult detectZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::ZoneDetectionResult detectMultiZone(const QPointF&) const override
    {
        return {};
    }
    PhosphorZones::Zone* zoneAtPoint(const QPointF&) const override
    {
        return nullptr;
    }
    PhosphorZones::Zone* nearestZone(const QPointF&) const override
    {
        return nullptr;
    }
    QVector<PhosphorZones::Zone*> expandPaintedZonesToRect(const QVector<PhosphorZones::Zone*>&) const override
    {
        return {};
    }
    void highlightZone(PhosphorZones::Zone*) override
    {
    }
    void highlightZones(const QVector<PhosphorZones::Zone*>&) override
    {
    }
    void clearHighlights() override
    {
    }
};

/**
 * @brief Shared fixture for the SnapEngine test suite: screen routing,
 *        lifecycle, float state, signal emission, and persistence delegation.
 *
 * Concrete test classes derive from this base and add their own private
 * Q_SLOTS; init()/cleanup() and the log-capture helpers are inherited.
 */
class SnapEngineTestFixture : public QObject
{
    Q_OBJECT

protected:
    // Isolates XDG_DATA_HOME / XDG_CONFIG_HOME under a temp dir so the
    // LayoutRegistry's default "plasmazones/layouts" subdir resolves into
    // the temp dir instead of ~/.local/share/plasmazones/layouts/.
    // Without this every test run leaks a "TestLayout-<uuid>.json" into
    // the user's real layouts dir — by April 2026 the directory had
    // accumulated >100 stale TestLayouts from prior CI / dev test runs,
    // showing up duplicated in the layout picker overlay.
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    StubZoneDetectorSnap* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_wts = nullptr;
    PhosphorSnapEngine::SnapState* m_snapState = nullptr;

protected Q_SLOTS:

    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new StubZoneDetectorSnap(nullptr);
        m_wts = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
        m_snapState = new PhosphorSnapEngine::SnapState(QString(), nullptr);
        m_wts->setSnapState(m_snapState);
    }

    void cleanup()
    {
        m_wts->setSnapState(nullptr);
        delete m_snapState;
        delete m_wts;
        delete m_zoneDetector;
        delete m_settings;
        delete m_layoutManager;
        m_guard.reset();
    }

protected:
    static QStringList& gateLogSink()
    {
        static QStringList sink;
        return sink;
    }
    static void gateLogHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
    {
        gateLogSink().append(msg);
    }

    /// Run calculateSnapToEmptyZone with the given gate inputs and capture
    /// debug logs from the snap-engine category. Returns the captured lines.
    QStringList runGate(SnapEngine& engine, PhosphorZones::Layout* layout, bool perLayoutAuto, bool globalAuto,
                        const QString& screenId)
    {
        layout->setAutoAssign(perLayoutAuto);
        m_settings->setAutoAssignAllLayouts(globalAuto);

        QLoggingCategory::setFilterRules(QStringLiteral("org.phosphor.snap-engine.debug=true"));
        gateLogSink().clear();
        QtMessageHandler prev = qInstallMessageHandler(&SnapEngineTestFixture::gateLogHandler);

        // Result is intentionally ignored — geometry resolution depends on a
        // wired ScreenManager, which a guiless fixture doesn't provide. The
        // log line tells us which branch executed, which is what the gate
        // contract is actually about.
        (void)engine.calculateSnapToEmptyZone(QStringLiteral("app|uuid-gate"), screenId, /*isSticky*/ false);

        qInstallMessageHandler(prev);
        QLoggingCategory::setFilterRules(QString());
        return gateLogSink();
    }

    /// Run resolveWindowRestore while capturing snap-engine debug logs, so a
    /// test can assert WHICH branch produced the result — the disabled-context
    /// gate logs a distinctive line. Mirrors runGate()'s capture pattern.
    QStringList captureResolveLogs(SnapEngine& engine, const QString& windowId, const QString& screenId,
                                   PhosphorEngine::SnapResult* outResult)
    {
        QLoggingCategory::setFilterRules(QStringLiteral("org.phosphor.snap-engine.debug=true"));
        gateLogSink().clear();
        QtMessageHandler prev = qInstallMessageHandler(&SnapEngineTestFixture::gateLogHandler);

        const PhosphorEngine::SnapResult result = engine.resolveWindowRestore(windowId, screenId, /*sticky*/ false);
        if (outResult) {
            *outResult = result;
        }

        qInstallMessageHandler(prev);
        QLoggingCategory::setFilterRules(QString());
        return gateLogSink();
    }
};
