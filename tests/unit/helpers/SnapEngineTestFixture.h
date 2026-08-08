// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QTest>
#include <QSignalSpy>
#include <QLoggingCategory>

#include "helpers/LogCapture.h"

#include <memory>
#include <optional>

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

// The already-included shared stub is the same inert placeholder: SnapEngine
// stores the detector and never calls it. Keeping a Q_OBJECT copy in this
// shared header also forced AUTOMOC wiring on every consuming target.
using StubZoneDetectorSnap = PlasmaZones::StubZoneDetector;

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
        m_wts = new PhosphorPlacement::WindowTrackingService(m_layoutManager, nullptr, nullptr);
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
    /// The snap-engine category, captured through the shared helper (see
    /// LogCapture.h for why both severities and the category filter are
    /// load-bearing). Held as an optional so runGate/captureResolveLogs can
    /// wrap differently-shaped calls between arm and disarm.
    std::optional<PlasmaZones::TestHelpers::CategoryLogCapture> m_capture;

    void beginLogCapture()
    {
        m_capture.emplace(QLatin1String("org.phosphor.snap-engine"));
    }

    QStringList endLogCapture()
    {
        const QStringList lines = m_capture ? m_capture->lines() : QStringList{};
        m_capture.reset();
        return lines;
    }

    /// Run calculateSnapToEmptyZone with the given gate inputs and capture
    /// debug logs from the snap-engine category. Returns the captured lines.
    QStringList runGate(SnapEngine& engine, PhosphorZones::Layout* layout, bool perLayoutAuto, bool globalAuto,
                        const QString& screenId)
    {
        layout->setAutoAssign(perLayoutAuto);
        m_settings->setAutoAssignAllLayouts(globalAuto);

        beginLogCapture();

        // Result is intentionally ignored — geometry resolution depends on a
        // wired ScreenManager, which a guiless fixture doesn't provide. The
        // log line tells us which branch executed, which is what the gate
        // contract is actually about.
        (void)engine.calculateSnapToEmptyZone(QStringLiteral("app|uuid-gate"), screenId, /*isSticky*/ false);

        return endLogCapture();
    }

    /// Run resolveWindowRestore while capturing snap-engine debug logs, so a
    /// test can assert WHICH branch produced the result — the disabled-context
    /// gate logs a distinctive line. Mirrors runGate()'s capture pattern.
    QStringList captureResolveLogs(SnapEngine& engine, const QString& windowId, const QString& screenId,
                                   PhosphorEngine::SnapResult* outResult)
    {
        beginLogCapture();

        const PhosphorEngine::SnapResult result = engine.resolveWindowRestore(windowId, screenId, /*sticky*/ false);
        if (outResult) {
            *outResult = result;
        }

        return endLogCapture();
    }

    /// Positive control for absence-based assertions: every resolveWindowRestore
    /// terminates in a logging branch, so an empty capture means the harness
    /// is broken, not that the asserted branch stayed silent — without it a
    /// `!contains(...)` assertion passes green against a sink that recorded
    /// nothing at all. Returns rather than asserting so the caller can
    /// QVERIFY it and stop the slot, the same reason captureClose returns a
    /// bool: a QVERIFY inside a helper returns only from the helper.
    ///
    /// Scoped to resolveWindowRestore-based tests. runGate wraps
    /// calculateSnapToEmptyZone, whose gate-PASS path logs nothing, so an
    /// empty capture there is the correct outcome and this control must not
    /// be applied to it.
    [[nodiscard]] static bool captureIsNonEmpty(const QStringList& lines)
    {
        return !lines.isEmpty();
    }
};
