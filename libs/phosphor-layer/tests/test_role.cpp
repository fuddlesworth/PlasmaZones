// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/Patterns.h>
#include <PhosphorLayer/Role.h>

#include <QMetaEnum>
#include <QTest>

using namespace PhosphorLayer;

class TestRole : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fluentModifiersReturnCopies()
    {
        const Role base = Patterns::Hud;
        const Role modified = base.withLayer(Layer::Background);
        QCOMPARE(base.layer, Layer::Overlay); // base unchanged
        QCOMPARE(modified.layer, Layer::Background);
        QCOMPARE(modified.anchors, base.anchors); // everything else carried over
        QCOMPARE(modified.scopePrefix, base.scopePrefix);
    }

    void fluentChainComposes()
    {
        const Role r = Patterns::Modal.withLayer(Layer::Top)
                           .withKeyboard(KeyboardInteractivity::Exclusive)
                           .withMargins(QMargins(10, 20, 30, 40))
                           .withScopePrefix(QStringLiteral("my-modal"));
        QCOMPARE(r.layer, Layer::Top);
        QCOMPARE(r.keyboard, KeyboardInteractivity::Exclusive);
        QCOMPARE(r.defaultMargins, QMargins(10, 20, 30, 40));
        QCOMPARE(r.scopePrefix, QStringLiteral("my-modal"));
        QCOMPARE(r.anchors, AnchorNone); // inherited from Modal
    }

    void presetsCoverDocumentedVocabulary()
    {
        // Hud: every anchor, overlay layer, kbd none.
        QCOMPARE(Patterns::Hud.layer, Layer::Overlay);
        QCOMPARE(Patterns::Hud.anchors, AnchorAll);
        QCOMPARE(Patterns::Hud.keyboard, KeyboardInteractivity::None);
        QCOMPARE(Patterns::Hud.exclusiveZone, -1);

        // Panel(Top) reserves exclusive zone via 0 + kbd on-demand
        const Role topPanel = Patterns::Panel(Patterns::Edge::Top);
        QCOMPARE(topPanel.layer, Layer::Top);
        QCOMPARE(topPanel.exclusiveZone, 0);
        QCOMPARE(topPanel.keyboard, KeyboardInteractivity::OnDemand);
        QVERIFY(topPanel.anchors.testFlag(Anchor::Top));
        QVERIFY(topPanel.anchors.testFlag(Anchor::Left));
        QVERIFY(topPanel.anchors.testFlag(Anchor::Right));
        QVERIFY(!topPanel.anchors.testFlag(Anchor::Bottom));

        // Modal has no anchors (compositor centres) and exclusive kbd.
        QCOMPARE(Patterns::Modal.anchors, AnchorNone);
        QCOMPARE(Patterns::Modal.keyboard, KeyboardInteractivity::Exclusive);

        // Wallpaper: bottom-most layer, all anchors, zone 0 so it doesn't
        // shove other surfaces.
        QCOMPARE(Patterns::Wallpaper.layer, Layer::Background);
        QCOMPARE(Patterns::Wallpaper.anchors, AnchorAll);
        QCOMPARE(Patterns::Wallpaper.exclusiveZone, 0);
    }

    void scopePrefixIsPresetSpecific()
    {
        // Each pattern + variation has a distinct scope prefix so the
        // compositor can namespace surfaces independently.
        const QStringList prefixes{
            Patterns::Hud.scopePrefix,
            Patterns::Panel(Patterns::Edge::Top).scopePrefix,
            Patterns::Panel(Patterns::Edge::Bottom).scopePrefix,
            Patterns::Panel(Patterns::Edge::Left).scopePrefix,
            Patterns::Panel(Patterns::Edge::Right).scopePrefix,
            Patterns::Modal.scopePrefix,
            Patterns::Toast(Patterns::Corner::TopRight).scopePrefix,
            Patterns::Wallpaper.scopePrefix,
            Patterns::Floating.scopePrefix,
        };
        for (const auto& p : prefixes) {
            QVERIFY2(!p.isEmpty(), qPrintable(QStringLiteral("Empty prefix: %1").arg(p)));
        }
        const QSet<QString> unique(prefixes.begin(), prefixes.end());
        QCOMPARE(unique.size(), prefixes.size());
    }

    void equalityOperator()
    {
        QCOMPARE(Patterns::Hud, Patterns::Hud);
        QVERIFY(Patterns::Hud != Patterns::Panel(Patterns::Edge::Top));
        const Role custom = Patterns::Hud.withScopePrefix(QStringLiteral("foo"));
        QVERIFY(custom != Patterns::Hud);
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
        Role overlayWithZone = Patterns::Hud;
        overlayWithZone.exclusiveZone = 0;
        QVERIFY(!overlayWithZone.isValid());

        // AnchorNone + non-zero default margins: margins have no referent
        // to offset from, so the compositor discards them. The role as a
        // whole is malformed.
        Role floatingWithMargins = Patterns::Floating.withMargins(QMargins(10, 10, 10, 10));
        QVERIFY(!floatingWithMargins.isValid());

        // Well-formed presets pass.
        QVERIFY(Patterns::Hud.isValid());
        QVERIFY(Patterns::Modal.isValid());
        QVERIFY(Patterns::Panel(Patterns::Edge::Top).isValid());
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

    void namespaceMetaObjectLinked()
    {
        // Q_NAMESPACE_EXPORT + Q_ENUM_NS registers enum metadata on the
        // namespace's staticMetaObject. If AUTOMOC doesn't emit the moc
        // file for Role.h (or if the generated TU is dropped by the
        // linker), QMetaEnum::fromType<Layer>() returns an invalid
        // QMetaEnum and valueToKey returns nullptr. This is a linker-
        // behaviour regression test: catches the case where a future
        // refactor moves Q_NAMESPACE_EXPORT into a header AUTOMOC no
        // longer processes.
        const auto layerEnum = QMetaEnum::fromType<Layer>();
        QVERIFY2(layerEnum.isValid(), "Layer metaenum did not register — moc_Role may not be linked");
        QCOMPARE(QLatin1String(layerEnum.valueToKey(static_cast<int>(Layer::Overlay))), QLatin1String("Overlay"));

        const auto kbdEnum = QMetaEnum::fromType<KeyboardInteractivity>();
        QVERIFY2(kbdEnum.isValid(), "KeyboardInteractivity metaenum did not register");
        QCOMPARE(QLatin1String(kbdEnum.valueToKey(static_cast<int>(KeyboardInteractivity::Exclusive))),
                 QLatin1String("Exclusive"));
    }
};

QTEST_APPLESS_MAIN(TestRole)
#include "test_role.moc"
