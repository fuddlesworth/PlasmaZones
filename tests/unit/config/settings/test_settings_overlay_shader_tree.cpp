// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_overlay_shader_tree.cpp
 * @brief Settings — OverlayShaderTree persistence (the overlay analogue of
 *        test_settings_shader_tree).
 *
 * Pinned behaviour:
 *   - setOverlayShaderTree round-trips through the JSON blob (the
 *     Snapping.OverlayShaders schema group must declare the key or
 *     PhosphorConfig::Store::write drops the blob silently)
 *   - a fresh Settings instance on the same config reads the value back
 *     (the daemon-reads-what-the-settings-app-wrote path)
 *   - a same-tree write is a no-op: no spurious overlayShaderTreeChanged
 *   - the JSON facade parses and routes through the typed setter
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kLayoutId = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
}

class TestSettingsOverlayShaderTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testOverlayShaderTree_setRoundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;
        {
            Settings a;
            OverlayShaderTree tree;
            tree.setBaseline({QStringLiteral("cosmic-flow"), {{QStringLiteral("speed"), 1.5}}});
            tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {}});
            QSignalSpy spy(&a, &Settings::overlayShaderTreeChanged);
            a.setOverlayShaderTree(tree);
            QCOMPARE(spy.count(), 1);

            const OverlayShaderTree reread = a.overlayShaderTree();
            QCOMPARE(reread.baseline().shaderId, QStringLiteral("cosmic-flow"));
            QCOMPARE(reread.resolve(kLayoutId).shaderId, QStringLiteral("neon-city"));

            // Writing the identical tree back must not fire the signal.
            a.setOverlayShaderTree(reread);
            QCOMPARE(spy.count(), 1);
        }
        {
            // Fresh instance, same isolated config: the cross-process path.
            Settings b;
            const OverlayShaderTree reread = b.overlayShaderTree();
            QVERIFY(reread.hasOverride(kLayoutId));
            QCOMPARE(reread.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 1.5);
        }
    }

    void testOverlayShaderTree_jsonFacadeRoutesThroughTypedSetter()
    {
        IsolatedConfigGuard guard;
        Settings s;
        OverlayShaderTree tree;
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {}});
        const QString json = QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));

        QSignalSpy spy(&s, &Settings::overlayShaderTreeChanged);
        s.setOverlayShaderTreeJson(json);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s.overlayShaderTree().resolve(kLayoutId).shaderId, QStringLiteral("neon-city"));

        // Malformed JSON is ignored, not treated as a clear.
        s.setOverlayShaderTreeJson(QStringLiteral("not json"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(s.overlayShaderTree().hasOverride(kLayoutId));

        // Empty string resets to the empty tree.
        s.setOverlayShaderTreeJson(QString());
        QCOMPARE(spy.count(), 2);
        QVERIFY(s.overlayShaderTree().isEmpty());
    }
};

QTEST_MAIN(TestSettingsOverlayShaderTree)
#include "test_settings_overlay_shader_tree.moc"
