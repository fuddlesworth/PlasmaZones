// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_adaptor_active_layouts.cpp
 *
 * Pins the setActiveLayouts contract on TilingAdaptor: the daemon pushes the
 * per-screen rules-visible active layout map unconditionally from every
 * updateEngineScreens pass, and the adaptor's emit-on-change gate must
 * collapse identical pushes to zero broadcasts while a changed map reaches
 * the wire exactly once and is readable back through the property getter.
 *
 * NOTE: the adaptor is parented to a plain QObject rather than a
 * D-Bus-registered object, same as the panel-gate test — QDBusAbstractAdaptor
 * walks its parent's meta-object at construction and a vanilla QObject parent
 * sidesteps the D-Bus registration assumptions.
 */

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QObject>

#include "dbus/tilingadaptor/tilingadaptor.h"

using namespace PlasmaZones;

class TestTilingAdaptorActiveLayouts : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testChangeGateAndReadback()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::activeLayoutsChanged);
        QVERIFY(spy.isValid());

        // Starts empty: no push has happened.
        QVERIFY(adaptor.activeLayouts().isEmpty());

        QVariantMap mapA;
        mapA.insert(QStringLiteral("HDMI-1"), QStringLiteral("scrolling:{11111111-1111-1111-1111-111111111111}"));
        mapA.insert(QStringLiteral("DP-2"), QStringLiteral("autotile:master-stack"));

        adaptor.setActiveLayouts(mapA);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toMap(), mapA);
        QCOMPARE(adaptor.activeLayouts(), mapA);

        // Identical push (the daemon's unconditional tail): no re-broadcast.
        adaptor.setActiveLayouts(mapA);
        QCOMPARE(spy.count(), 1);

        // One screen's value flips (template cleared → bare sentinel): one
        // new broadcast carrying the full map.
        QVariantMap mapB = mapA;
        mapB.insert(QStringLiteral("HDMI-1"), QStringLiteral("scrolling:"));
        adaptor.setActiveLayouts(mapB);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toMap(), mapB);
        QCOMPARE(adaptor.activeLayouts(), mapB);
    }

    void testEmptyMapIsAChange()
    {
        QObject adaptorParent;
        TilingAdaptor adaptor(nullptr, &adaptorParent);
        QSignalSpy spy(&adaptor, &TilingAdaptor::activeLayoutsChanged);

        QVariantMap map;
        map.insert(QStringLiteral("HDMI-1"), QStringLiteral("{22222222-2222-2222-2222-222222222222}"));
        adaptor.setActiveLayouts(map);
        QCOMPARE(spy.count(), 1);

        // All screens gone (e.g. every output disconnected) is a genuine
        // change and must broadcast so the effect drops its stale stamps.
        adaptor.setActiveLayouts(QVariantMap());
        QCOMPARE(spy.count(), 2);
        QVERIFY(adaptor.activeLayouts().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTilingAdaptorActiveLayouts)
#include "test_tiling_adaptor_active_layouts.moc"
