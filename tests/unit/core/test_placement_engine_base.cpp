// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QSignalSpy>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/IPlacementState.h>

using namespace PhosphorEngine;

class ConcreteEngine : public PlacementEngineBase
{
    Q_OBJECT

public:
    explicit ConcreteEngine(QObject* parent = nullptr)
        : PlacementEngineBase(parent)
    {
    }

    bool isActiveOnScreen(const QString&) const override
    {
        return true;
    }
    void windowOpened(const QString&, const QString&, int, int) override
    {
    }
    void windowClosed(const QString&) override
    {
    }
    void windowFocused(const QString&, const QString&) override
    {
    }
    void toggleWindowFloat(const QString&, const QString&) override
    {
    }
    void setWindowFloat(const QString&, bool) override
    {
    }
    void focusInDirection(const QString&, const NavigationContext&) override
    {
    }
    void moveFocusedInDirection(const QString&, const NavigationContext&) override
    {
    }
    void swapFocusedInDirection(const QString&, const NavigationContext&) override
    {
    }
    void moveFocusedToPosition(int, const NavigationContext&) override
    {
    }
    void rotateWindows(bool, const NavigationContext&) override
    {
    }
    void reapplyLayout(const NavigationContext&) override
    {
    }
    void snapAllWindows(const NavigationContext&) override
    {
    }
    void cycleFocus(bool, const NavigationContext&) override
    {
    }
    void pushToEmptyZone(const NavigationContext&) override
    {
    }
    void restoreFocusedWindow(const NavigationContext&) override
    {
    }
    void toggleFocusedFloat(const NavigationContext&) override
    {
    }
    void saveState() override
    {
    }
    void loadState() override
    {
    }
    IPlacementState* stateForScreen(const QString&) override
    {
        return nullptr;
    }
    const IPlacementState* stateForScreen(const QString&) const override
    {
        return nullptr;
    }
};

// The per-engine unmanaged-geometry store and WindowState FSM were removed from
// PlacementEngineBase: float-back / free geometry now lives solely in the unified
// WindowPlacementStore (one record per window, shared freeGeometryByScreen). The
// base class is a thin shell — settings injection and a no-op base prune — so this
// test covers only that remaining surface. Float-back behavior is covered by the
// WindowPlacementStore + WindowTrackingService + WTA tests.
class TestPlacementEngineBase : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testEngineSettings_setAndGet()
    {
        ConcreteEngine engine;
        QCOMPARE(engine.engineSettings(), nullptr);

        QObject settings;
        engine.setEngineSettings(&settings);
        QCOMPARE(engine.engineSettings(), &settings);
    }

    void testEngineSettings_nullptrIgnored()
    {
        ConcreteEngine engine;
        QObject settings;
        engine.setEngineSettings(&settings);

        engine.setEngineSettings(nullptr); // rejected — keeps the prior value
        QCOMPARE(engine.engineSettings(), &settings);
    }

    void testPruneStaleWindows_baseKeepsNoState()
    {
        // The base no longer holds per-window state, so its prune is a no-op (0).
        // Engines override and add their own pruning.
        ConcreteEngine engine;
        QCOMPARE(engine.pruneStaleWindows({QStringLiteral("alive")}), 0);
    }
};

QTEST_MAIN(TestPlacementEngineBase)
#include "test_placement_engine_base.moc"
