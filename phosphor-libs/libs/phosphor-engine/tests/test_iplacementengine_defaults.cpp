// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_iplacementengine_defaults.cpp
 * @brief The IPlacementEngine interface DEFAULTS, executed.
 *
 * Every shipped engine overrides the optional capability hooks, so their
 * defaults were dead code with no executable contract — an embedder's
 * minimal engine is the only consumer, and a silent default change would
 * reach it untested. This stub is the first IPlacementEngine subclass that
 * implements ONLY the pure virtuals, pinning what a minimal engine gets:
 * LayoutSupport::None (the daemon then suppresses the layout picker and the
 * layout shortcuts answer with the "not available" OSD), no placement
 * capture/restore, no drag preview, no algorithm identity.
 */

#include <QPoint>
#include <QRegularExpression>
#include <QTest>

#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/IPlacementEngine.h>

using namespace PhosphorEngine;

namespace {

/// Pure-virtuals only — every optional hook stays on its interface default.
class MinimalEngine : public IPlacementEngine
{
public:
    bool isActiveOnScreen(const QString&) const override
    {
        return false;
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
    void setWindowFloat(const QString&, bool, const QString&) override
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

/// The minimal engine plus a record of the per-removal pair, the two hooks
/// the remapDesktops default decomposes a single removal into.
class RenumberRecordingEngine : public MinimalEngine
{
public:
    void pruneStatesForDesktop(int removedDesktop) override
    {
        calls.append(QStringLiteral("prune %1").arg(removedDesktop));
    }
    void renumberDesktopsAfterRemoval(int removedDesktop) override
    {
        calls.append(QStringLiteral("renumber %1").arg(removedDesktop));
    }
    QStringList calls;
};

} // namespace

class TestIPlacementEngineDefaults : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testLayoutSupport_defaultsToNone()
    {
        // The load-bearing default: an engine must OPT IN to being a layout
        // consumer. The daemon's layoutSupportForScreen gates read this, and
        // all three shipped engines override it (Placement / Placement /
        // Templates) — this stub is the only executable proof the interface
        // answer is None.
        MinimalEngine engine;
        QCOMPARE(engine.layoutSupport(), IPlacementEngine::LayoutSupport::None);
    }

    void testProvidesDragInsertSelector_defaultsToFalse()
    {
        // Same opt-in discipline as layoutSupport: the drag popup renders an
        // engine's drag-insert vocabulary only when the engine claims it.
        // WindowDragAdaptor's trigger gate reads this to keep the popup off
        // engine-owned screens by default (autotile relies on that).
        MinimalEngine engine;
        QVERIFY(!engine.providesDragInsertSelector());
    }

    void testCapabilityDefaults_areInert()
    {
        MinimalEngine engine;
        // No placement persistence: capture answers nothing, restore refuses.
        QVERIFY(!engine.capturePlacement(QStringLiteral("win")).has_value());
        QVERIFY(!engine.restorePlacement(WindowPlacement{}, QStringLiteral("DP-1")));
        QVERIFY(!engine.claimCrossScreenReopen(QStringLiteral("win"), QStringLiteral("DP-1"), 0, 0));
        // No drag-insert preview: nothing is live, begin refuses to start one,
        // both screen ids and the indicator rect stay empty, and hit-testing a
        // cursor position answers an invalid target (primary < 0). An embedder
        // that paints a drop indicator off these must see the refusal, not a
        // rect at the origin.
        QVERIFY(!engine.hasDragInsertPreview());
        QVERIFY(!engine.beginDragInsertPreview(QStringLiteral("win"), QStringLiteral("DP-1")));
        QVERIFY(engine.dragInsertPreviewScreenId().isEmpty());
        QVERIFY(engine.dragInsertPreviewPriorScreenId().isEmpty());
        QVERIFY(!engine.computeDragInsertTargetAtPoint(QStringLiteral("DP-1"), QPoint(100, 100)).isValid());
        QVERIFY(engine.dragInsertIndicatorRect(QStringLiteral("DP-1")).isEmpty());
        // Unlimited-window sentinel, not a real cap: callers must never divide
        // by it.
        QCOMPARE(engine.runtimeMaxWindows(), -1);
        // No identity of any kind, and not enabled.
        QVERIFY(engine.engineId().isEmpty());
        QVERIFY(!engine.isEnabled());
        QVERIFY(engine.algorithmId().isEmpty());
    }

    void remapDesktops_defaultDecomposesRemovals()
    {
        // An engine without its own remap still follows the change the daemon
        // used to drive by hand: prune the removed desktop, then shift the rest
        // down, in that order (F763). Several removals go highest first, so
        // each later position is still the one the map names.
        RenumberRecordingEngine engine;
        engine.remapDesktops(DesktopRemap::removal(2, 4));
        QCOMPARE(engine.calls, (QStringList{QStringLiteral("prune 2"), QStringLiteral("renumber 2")}));

        engine.calls.clear();
        engine.remapDesktops(DesktopRemap::fromIds(
            {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")},
            {QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("e")}));
        QCOMPARE(engine.calls,
                 (QStringList{QStringLiteral("prune 4"), QStringLiteral("renumber 4"), QStringLiteral("prune 2"),
                              QStringLiteral("renumber 2")}));

        // A rename or an append moves no number: nothing runs.
        engine.calls.clear();
        engine.remapDesktops(DesktopRemap::fromIds({QStringLiteral("a"), QStringLiteral("b")},
                                                   {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QVERIFY(engine.calls.isEmpty());

        // A reorder is not a removal: the pair cannot apply it, so the default
        // applies nothing and says so.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("remapDesktops:.*has no remap")));
        engine.remapDesktops(DesktopRemap::fromIds({QStringLiteral("a"), QStringLiteral("b")},
                                                   {QStringLiteral("b"), QStringLiteral("a")}));
        QVERIFY(engine.calls.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestIPlacementEngineDefaults)
#include "test_iplacementengine_defaults.moc"
