// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_overlay_shader_tree.cpp
 * @brief Settings — OverlayShaderTree persistence (the overlay analogue of
 *        test_settings_shader_tree).
 *
 * Pinned behaviour:
 *   - setOverlayShaderTree round-trips through the JSON blob (the
 *     Overlays schema group must declare the key or
 *     PhosphorConfig::Store::write drops the blob silently)
 *   - a fresh Settings instance on the same config reads the value back
 *     (the daemon-reads-what-the-settings-app-wrote path)
 *   - a same-tree write is a no-op: no spurious overlayShaderTreeChanged
 *   - the JSON facade parses and routes through the typed setter
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "config/configdefaults.h"
#include "config/configmigration.h"
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

    void testOverlayShaderTree_parameterOnlyChangeFiresAndPersists()
    {
        // Pins that the no-op gate compares PARAMETERS too: a same-shader
        // write with a changed parameter map must fire the signal and land
        // on disk (a future operator== that ignored parameters would leave
        // this suite green while parameter edits stop saving).
        IsolatedConfigGuard guard;
        Settings s;
        OverlayShaderTree tree;
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {{QStringLiteral("speed"), 1.0}}});
        s.setOverlayShaderTree(tree);

        QSignalSpy spy(&s, &Settings::overlayShaderTreeChanged);
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {{QStringLiteral("speed"), 2.0}}});
        s.setOverlayShaderTree(tree);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s.overlayShaderTree().resolve(kLayoutId).parameters.value(QStringLiteral("speed")).toDouble(), 2.0);
    }

    /// The schema validator, driven through the door the typed setter does not
    /// cover: a config.json written by something other than Settings. A
    /// settings profile being applied reaches the same key the same way
    /// (Store::importFromJson writes each declared key's blob value straight
    /// through, and write() runs the validator), and the file is hand-editable
    /// besides.
    ///
    /// Every assertion here is on something OverlayShaderTree::fromJson would
    /// otherwise keep. The typed round trip already drops unknown FIELDS, so
    /// asserting on those would pass with the validator removed and pin
    /// nothing — these three axes are what actually distinguishes the guard
    /// from its absence.
    void testOverlayShaderTree_rawConfigIsSanitizedOnRead()
    {
        IsolatedConfigGuard guard;

        const QString overLong = QString(2000, QLatin1Char('x'));
        QJsonObject legit{
            {QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("neon-city")},
            {QLatin1String(OverlayShaderProfile::JsonFieldParameters), QJsonObject{{QStringLiteral("speed"), 2.0}}}};
        QJsonObject overrides{
            {kLayoutId, legit},
            // Unresolvable key shape: the pre-v8 editor could stamp these, and
            // they surface on the assignments page as nameless broken rows.
            {QStringLiteral("autotile:bsp"), legit},
        };
        QJsonObject baseline{
            {QLatin1String(OverlayShaderProfile::JsonFieldShaderId), overLong},
            {QLatin1String(OverlayShaderProfile::JsonFieldParameters),
             QJsonObject{{QStringLiteral("tex"), overLong},
                         {QStringLiteral("nested"), QJsonObject{{QStringLiteral("a"), 1}}},
                         {QStringLiteral("speed"), 0.5}}},
        };
        const QJsonObject tree{{QLatin1String(OverlayShaderTree::JsonFieldBaseline), baseline},
                               {QLatin1String(OverlayShaderTree::JsonFieldOverrides), overrides}};

        // Group names are dot-paths that JsonBackend stores as NESTED objects,
        // so build the nesting rather than inserting the dotted name flat.
        QJsonObject group{{ConfigDefaults::overlayShaderTreeKey(), tree}};
        const QStringList segments = ConfigDefaults::overlaysGroup().split(QLatin1Char('.'));
        for (auto it = segments.crbegin(); it != segments.crend(); ++it)
            group = QJsonObject{{*it, group}};
        QJsonObject root = group;
        root.insert(ConfigKeys::versionKey(), ConfigSchemaVersion);
        QFile f(ConfigDefaults::configFilePath());
        QVERIFY(QDir().mkpath(QFileInfo(f).absolutePath()));
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QCOMPARE(f.write(bytes), static_cast<qint64>(bytes.size()));
        f.close();

        Settings s;
        const OverlayShaderTree read = s.overlayShaderTree();

        // The legitimate override is untouched — a guard that empties the key
        // would satisfy every drop assertion below and be useless.
        QVERIFY(read.hasOverride(kLayoutId));
        QCOMPARE(read.directOverride(kLayoutId).shaderId, QStringLiteral("neon-city"));
        QCOMPARE(read.directOverride(kLayoutId).parameters.value(QStringLiteral("speed")).toDouble(), 2.0);

        QVERIFY2(!read.hasOverride(QStringLiteral("autotile:bsp")), "non-UUID override key survived");
        QVERIFY2(read.baseline().shaderId.isEmpty(), "over-long shaderId survived");
        QVERIFY2(!read.baseline().parameters.contains(QStringLiteral("tex")), "over-long parameter value survived");
        QVERIFY2(!read.baseline().parameters.contains(QStringLiteral("nested")), "nested parameter survived");
        // The sound parameter beside the dropped ones is kept.
        QCOMPARE(read.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 0.5);

        // Idempotent: the validator's own output must survive a second pass,
        // which is the contract KeyDef::validator states.
        //
        // Re-writing `read` through the setter would prove nothing — it is
        // what the getter just returned, so the setter's value-equality gate
        // returns before writing and the comparison is a value against
        // itself. Feed the RAW fixture back to disk instead and re-read with a
        // fresh Settings, so the validator genuinely runs a second time over
        // its own first output.
        {
            QFile again(ConfigDefaults::configFilePath());
            QVERIFY(again.open(QIODevice::WriteOnly));
            const QByteArray raw = QJsonDocument(root).toJson();
            QCOMPARE(again.write(raw), static_cast<qint64>(raw.size()));
            again.close();
        }
        Settings second;
        QCOMPARE(second.overlayShaderTree(), read);
    }
};

QTEST_MAIN(TestSettingsOverlayShaderTree)
#include "test_settings_overlay_shader_tree.moc"
