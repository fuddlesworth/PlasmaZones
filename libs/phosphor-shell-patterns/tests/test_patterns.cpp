// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellPatterns/Patterns.h>

#include <PhosphorLayer/Role.h>

#include <QTest>

using namespace PhosphorLayer;
namespace PSP = PhosphorShellPatterns;

class TestPatterns : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void hudIsFullscreenClickThroughOverlay()
    {
        QCOMPARE(PSP::Hud().layer, Layer::Overlay);
        QCOMPARE(PSP::Hud().anchors, AnchorAll);
        QCOMPARE(PSP::Hud().keyboard, KeyboardInteractivity::None);
        QCOMPARE(PSP::Hud().exclusiveZone, -1);
    }

    void wallpaperIsBackgroundLayerSpaceReserving()
    {
        QCOMPARE(PSP::Wallpaper().layer, Layer::Background);
        QCOMPARE(PSP::Wallpaper().anchors, AnchorAll);
        QCOMPARE(PSP::Wallpaper().exclusiveZone, 0);
    }

    void modalGrabsExclusiveKeyboard()
    {
        QCOMPARE(PSP::Modal().anchors, AnchorNone);
        QCOMPARE(PSP::Modal().keyboard, KeyboardInteractivity::Exclusive);
    }

    void floatingHasNoAnchors()
    {
        QCOMPARE(PSP::Floating().anchors, AnchorNone);
        QCOMPARE(PSP::Floating().layer, Layer::Overlay);
    }

    void panelReservesSpaceWithEdgeAnchors()
    {
        const Role topPanel = PSP::Panel(PSP::Edge::Top);
        QCOMPARE(topPanel.layer, Layer::Top);
        QCOMPARE(topPanel.exclusiveZone, 0);
        QCOMPARE(topPanel.keyboard, KeyboardInteractivity::OnDemand);
        QVERIFY(topPanel.anchors.testFlag(Anchor::Top));
        QVERIFY(topPanel.anchors.testFlag(Anchor::Left));
        QVERIFY(topPanel.anchors.testFlag(Anchor::Right));
        QVERIFY(!topPanel.anchors.testFlag(Anchor::Bottom));

        const Role leftPanel = PSP::Panel(PSP::Edge::Left);
        QVERIFY(leftPanel.anchors.testFlag(Anchor::Top));
        QVERIFY(leftPanel.anchors.testFlag(Anchor::Bottom));
        QVERIFY(leftPanel.anchors.testFlag(Anchor::Left));
        QVERIFY(!leftPanel.anchors.testFlag(Anchor::Right));
    }

    void toastAnchorsAreCornerSpecific()
    {
        const Role tr = PSP::Toast(PSP::Corner::TopRight);
        QVERIFY(tr.anchors.testFlag(Anchor::Top));
        QVERIFY(tr.anchors.testFlag(Anchor::Right));
        QVERIFY(!tr.anchors.testFlag(Anchor::Bottom));

        const Role bl = PSP::Toast(PSP::Corner::BottomLeft);
        QVERIFY(bl.anchors.testFlag(Anchor::Bottom));
        QVERIFY(bl.anchors.testFlag(Anchor::Left));
        QVERIFY(!bl.anchors.testFlag(Anchor::Top));
    }

    void scopePrefixesAreUnique()
    {
        // Each Pattern and variation has a distinct scope prefix so the
        // compositor can namespace surfaces independently.
        const QStringList prefixes{
            PSP::Hud().scopePrefix,
            PSP::Panel(PSP::Edge::Top).scopePrefix,
            PSP::Panel(PSP::Edge::Bottom).scopePrefix,
            PSP::Panel(PSP::Edge::Left).scopePrefix,
            PSP::Panel(PSP::Edge::Right).scopePrefix,
            PSP::Modal().scopePrefix,
            PSP::Toast(PSP::Corner::TopLeft).scopePrefix,
            PSP::Toast(PSP::Corner::TopRight).scopePrefix,
            PSP::Toast(PSP::Corner::BottomLeft).scopePrefix,
            PSP::Toast(PSP::Corner::BottomRight).scopePrefix,
            PSP::Wallpaper().scopePrefix,
            PSP::Floating().scopePrefix,
        };
        for (const auto& p : prefixes) {
            QVERIFY2(!p.isEmpty(), qPrintable(QStringLiteral("Empty prefix: %1").arg(p)));
        }
        const QSet<QString> unique(prefixes.begin(), prefixes.end());
        QCOMPARE(unique.size(), prefixes.size());
    }

    void allPresetsPassRoleIsValid()
    {
        QVERIFY(PSP::Hud().isValid());
        QVERIFY(PSP::Modal().isValid());
        QVERIFY(PSP::Wallpaper().isValid());
        QVERIFY(PSP::Floating().isValid());
        QVERIFY(PSP::Panel(PSP::Edge::Top).isValid());
        QVERIFY(PSP::Panel(PSP::Edge::Bottom).isValid());
        QVERIFY(PSP::Panel(PSP::Edge::Left).isValid());
        QVERIFY(PSP::Panel(PSP::Edge::Right).isValid());
        QVERIFY(PSP::Toast(PSP::Corner::TopLeft).isValid());
        QVERIFY(PSP::Toast(PSP::Corner::TopRight).isValid());
        QVERIFY(PSP::Toast(PSP::Corner::BottomLeft).isValid());
        QVERIFY(PSP::Toast(PSP::Corner::BottomRight).isValid());
    }

    void presetsAreSingletonReferences()
    {
        // The four named preset accessors return references to function-local
        // statics. Repeated calls must alias the same object so consumers can
        // capture by reference and compare addresses for identity.
        QCOMPARE(&PSP::Hud(), &PSP::Hud());
        QCOMPARE(&PSP::Modal(), &PSP::Modal());
        QCOMPARE(&PSP::Wallpaper(), &PSP::Wallpaper());
        QCOMPARE(&PSP::Floating(), &PSP::Floating());
    }
};

QTEST_APPLESS_MAIN(TestPatterns)
#include "test_patterns.moc"
