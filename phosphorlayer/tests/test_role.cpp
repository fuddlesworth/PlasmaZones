// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/Role.h>

#include <QTest>

using namespace PhosphorLayer;

class TestRole : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fluentModifiersReturnCopies()
    {
        const Role base = Roles::FullscreenOverlay;
        const Role modified = base.withLayer(Layer::Background);
        QCOMPARE(base.layer, Layer::Overlay); // base unchanged
        QCOMPARE(modified.layer, Layer::Background);
        QCOMPARE(modified.anchors, base.anchors); // everything else carried over
        QCOMPARE(modified.scopePrefix, base.scopePrefix);
    }

    void fluentChainComposes()
    {
        const Role r = Roles::CenteredModal.withLayer(Layer::Top)
                           .withKeyboard(KeyboardInteractivity::Exclusive)
                           .withMargins(QMargins(10, 20, 30, 40))
                           .withScopePrefix(QStringLiteral("my-modal"));
        QCOMPARE(r.layer, Layer::Top);
        QCOMPARE(r.keyboard, KeyboardInteractivity::Exclusive);
        QCOMPARE(r.defaultMargins, QMargins(10, 20, 30, 40));
        QCOMPARE(r.scopePrefix, QStringLiteral("my-modal"));
        QCOMPARE(r.anchors, AnchorNone); // inherited from CenteredModal
    }

    void presetsCoverDocumentedVocabulary()
    {
        // FullscreenOverlay: every anchor, overlay layer, kbd none.
        QCOMPARE(Roles::FullscreenOverlay.layer, Layer::Overlay);
        QCOMPARE(Roles::FullscreenOverlay.anchors, AnchorAll);
        QCOMPARE(Roles::FullscreenOverlay.keyboard, KeyboardInteractivity::None);
        QCOMPARE(Roles::FullscreenOverlay.exclusiveZone, -1);

        // TopPanel reserves exclusive zone via 0 + kbd on-demand
        QCOMPARE(Roles::TopPanel.layer, Layer::Top);
        QCOMPARE(Roles::TopPanel.exclusiveZone, 0);
        QCOMPARE(Roles::TopPanel.keyboard, KeyboardInteractivity::OnDemand);
        QVERIFY(Roles::TopPanel.anchors.testFlag(Anchor::Top));
        QVERIFY(Roles::TopPanel.anchors.testFlag(Anchor::Left));
        QVERIFY(Roles::TopPanel.anchors.testFlag(Anchor::Right));
        QVERIFY(!Roles::TopPanel.anchors.testFlag(Anchor::Bottom));

        // CenteredModal has no anchors (compositor centres) and exclusive kbd.
        QCOMPARE(Roles::CenteredModal.anchors, AnchorNone);
        QCOMPARE(Roles::CenteredModal.keyboard, KeyboardInteractivity::Exclusive);

        // Background: bottom-most layer, all anchors, zone 0 so it doesn't
        // shove other surfaces.
        QCOMPARE(Roles::Background.layer, Layer::Background);
        QCOMPARE(Roles::Background.anchors, AnchorAll);
        QCOMPARE(Roles::Background.exclusiveZone, 0);
    }

    void scopePrefixIsPresetSpecific()
    {
        // Each preset has a distinct scope prefix so the compositor can
        // namespace them independently.
        QStringList prefixes{
            Roles::FullscreenOverlay.scopePrefix, Roles::TopPanel.scopePrefix,   Roles::BottomPanel.scopePrefix,
            Roles::LeftDock.scopePrefix,          Roles::RightDock.scopePrefix,  Roles::CenteredModal.scopePrefix,
            Roles::CornerToast.scopePrefix,       Roles::Background.scopePrefix, Roles::FloatingOverlay.scopePrefix};
        for (const auto& p : prefixes) {
            QVERIFY2(!p.isEmpty(), qPrintable(QStringLiteral("Empty prefix: %1").arg(p)));
        }
        const QSet<QString> unique(prefixes.begin(), prefixes.end());
        QCOMPARE(unique.size(), prefixes.size());
    }

    void equalityOperator()
    {
        QCOMPARE(Roles::FullscreenOverlay, Roles::FullscreenOverlay);
        QVERIFY(Roles::FullscreenOverlay != Roles::TopPanel);
        const Role custom = Roles::FullscreenOverlay.withScopePrefix(QStringLiteral("foo"));
        QVERIFY(custom != Roles::FullscreenOverlay);
    }

    void isValidRejectsMalformedRoles()
    {
        // Empty scope.
        Role bad;
        bad.scopePrefix.clear();
        QVERIFY(!bad.isValid());

        // Overlay layer + non-negative exclusive zone (zone is ignored by
        // the compositor for overlay, but accepting it silently is a
        // consumer trap).
        Role overlayWithZone = Roles::FullscreenOverlay;
        overlayWithZone.exclusiveZone = 0;
        QVERIFY(!overlayWithZone.isValid());

        // AnchorNone + non-zero default margins: margins have no referent
        // to offset from, so the compositor discards them. The role as a
        // whole is malformed.
        Role floatingWithMargins = Roles::FloatingOverlay.withMargins(QMargins(10, 10, 10, 10));
        QVERIFY(!floatingWithMargins.isValid());

        // Well-formed presets pass.
        QVERIFY(Roles::FullscreenOverlay.isValid());
        QVERIFY(Roles::CenteredModal.isValid());
        QVERIFY(Roles::TopPanel.isValid());
    }

    void anchorFlagsCompose()
    {
        const Anchors sides = Anchor::Left | Anchor::Right;
        QVERIFY(sides.testFlag(Anchor::Left));
        QVERIFY(sides.testFlag(Anchor::Right));
        QVERIFY(!sides.testFlag(Anchor::Top));
        QVERIFY(!sides.testFlag(Anchor::Bottom));

        QCOMPARE(AnchorAll, Anchor::Top | Anchor::Bottom | Anchor::Left | Anchor::Right);
        QVERIFY(AnchorNone == Anchors());
    }
};

QTEST_APPLESS_MAIN(TestRole)
#include "test_role.moc"
