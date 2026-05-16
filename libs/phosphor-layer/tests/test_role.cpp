// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/Role.h>

#include <QMetaEnum>
#include <QTest>

using namespace PhosphorLayer;

namespace {

// Inline Role construction for the layer-only tests. The axis-2 Pattern
// recipes (Hud, Modal, Wallpaper, …) live in the sibling library
// `phosphor-shell-patterns`; this lib stays free of that dependency by
// constructing test fixtures inline. The shapes here mirror what the
// shell-patterns library returns for the equivalent named patterns.
Role hudFixture()
{
    return Role{Layer::Overlay, AnchorAll, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("test-hud")};
}

Role modalFixture()
{
    return Role{Layer::Top, AnchorNone, -1, KeyboardInteractivity::Exclusive, QMargins(), QStringLiteral("test-modal")};
}

Role topPanelFixture()
{
    return Role{Layer::Top, Anchor::Top | Anchor::Left | Anchor::Right,
                0,          KeyboardInteractivity::OnDemand,
                QMargins(), QStringLiteral("test-top-panel")};
}

Role floatingFixture()
{
    return Role{
        Layer::Overlay, AnchorNone, -1, KeyboardInteractivity::None, QMargins(), QStringLiteral("test-floating")};
}

} // namespace

class TestRole : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fluentModifiersReturnCopies()
    {
        const Role base = hudFixture();
        const Role modified = base.withLayer(Layer::Background);
        QCOMPARE(base.layer, Layer::Overlay); // base unchanged
        QCOMPARE(modified.layer, Layer::Background);
        QCOMPARE(modified.anchors, base.anchors); // everything else carried over
        QCOMPARE(modified.scopePrefix, base.scopePrefix);
    }

    void fluentChainComposes()
    {
        const Role r = modalFixture()
                           .withLayer(Layer::Top)
                           .withKeyboard(KeyboardInteractivity::Exclusive)
                           .withMargins(QMargins(10, 20, 30, 40))
                           .withScopePrefix(QStringLiteral("my-modal"));
        QCOMPARE(r.layer, Layer::Top);
        QCOMPARE(r.keyboard, KeyboardInteractivity::Exclusive);
        QCOMPARE(r.defaultMargins, QMargins(10, 20, 30, 40));
        QCOMPARE(r.scopePrefix, QStringLiteral("my-modal"));
        QCOMPARE(r.anchors, AnchorNone); // inherited from modal fixture
    }

    void equalityOperator()
    {
        const Role a = hudFixture();
        const Role b = hudFixture();
        QCOMPARE(a, b);
        QVERIFY(a != topPanelFixture());
        const Role custom = a.withScopePrefix(QStringLiteral("foo"));
        QVERIFY(custom != a);
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
        Role overlayWithZone = hudFixture();
        overlayWithZone.exclusiveZone = 0;
        QVERIFY(!overlayWithZone.isValid());

        // AnchorNone + non-zero default margins: margins have no referent
        // to offset from, so the compositor discards them. The role as a
        // whole is malformed.
        Role floatingWithMargins = floatingFixture().withMargins(QMargins(10, 10, 10, 10));
        QVERIFY(!floatingWithMargins.isValid());

        // Well-formed fixtures pass.
        QVERIFY(hudFixture().isValid());
        QVERIFY(modalFixture().isValid());
        QVERIFY(topPanelFixture().isValid());
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
