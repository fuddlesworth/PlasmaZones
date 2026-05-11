// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cross-checks every built-in animation shader's `#define <paramId>
// customParams[N].xyzw` / `#define <paramId> customColors[N]` lines
// against the order of `parameters[]` in its `metadata.json`.
// `AnimationShaderRegistry::translateAnimationParams` allocates UBO
// slots in declaration order — bool/float/int packed four-per-slot
// into `customParams`, colors one-per-slot into `customColors` —
// and the runtime can't detect a mismatch between metadata
// declaration order and the shader's `#define` slot map. A misordered
// pack silently delivers the wrong value to each name.
//
// The test parses both files for each pack and asserts every define
// line lands at the slot an in-order metadata walk would have given
// it. Catches author-side regressions at CI time instead of leaving
// them to be discovered by users running broken animations.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

class TestAnimationShaderParamWiring : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testEveryShaderParamWiring_data()
    {
        QTest::addColumn<QString>("packDir");
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        if (!dir.exists()) {
            QSKIP("data/animations not found — running outside source tree");
        }
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        bool any = false;
        for (const QString& sub : subdirs) {
            if (sub == QLatin1String("shared")) {
                continue;
            }
            const QString meta = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/metadata.json");
            const QString frag = animationsDir + QLatin1Char('/') + sub + QStringLiteral("/effect.frag");
            if (QFileInfo::exists(meta) && QFileInfo::exists(frag)) {
                QTest::newRow(qPrintable(sub)) << (animationsDir + QLatin1Char('/') + sub);
                any = true;
            }
        }
        if (!any) {
            QSKIP("no animation shader packs with metadata + effect.frag found");
        }
    }

    void testEveryShaderParamWiring()
    {
        QFETCH(QString, packDir);

        // ── Parse metadata.json ─────────────────────────────────────────
        QFile metaFile(packDir + QStringLiteral("/metadata.json"));
        QVERIFY2(metaFile.open(QIODevice::ReadOnly), qPrintable(metaFile.errorString()));
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(metaFile.readAll(), &err);
        QVERIFY2(err.error == QJsonParseError::NoError, qPrintable(err.errorString()));
        const QJsonArray params = doc.object().value(QLatin1String("parameters")).toArray();

        // Build the expected slot map by walking parameters[] in order
        // and applying the same allocation rule the runtime registry
        // uses (bool/int/float pack four-per-slot into customParams in
        // declaration order; color drains one-per-slot into customColors).
        struct ParamSlotExpectation
        {
            QString id;
            QString type; // "color" or "params"
            int slot = 0;
            int sub = 0; // 0..3 = .x..w; -1 for color (whole vec4)
        };
        QHash<QString, ParamSlotExpectation> expected;
        int floatSlot = 0;
        int floatSub = 0;
        int colorSlot = 0;
        for (const QJsonValue& v : params) {
            const QJsonObject obj = v.toObject();
            const QString id = obj.value(QLatin1String("id")).toString();
            const QString type = obj.value(QLatin1String("type")).toString();
            ParamSlotExpectation exp;
            exp.id = id;
            if (type == QLatin1String("color")) {
                exp.type = QStringLiteral("color");
                exp.slot = colorSlot;
                exp.sub = -1;
                ++colorSlot;
            } else {
                exp.type = QStringLiteral("params");
                exp.slot = floatSlot;
                exp.sub = floatSub;
                ++floatSub;
                if (floatSub >= 4) {
                    floatSub = 0;
                    ++floatSlot;
                }
            }
            expected.insert(id, exp);
        }

        // ── Parse effect.frag for `#define X customParams[N].abc` /
        // `#define X customColors[N]` lines ──────────────────────────────
        QFile fragFile(packDir + QStringLiteral("/effect.frag"));
        QVERIFY2(fragFile.open(QIODevice::ReadOnly), qPrintable(fragFile.errorString()));
        const QString fragText = QString::fromUtf8(fragFile.readAll());

        static const QRegularExpression definePattern(
            QStringLiteral(R"(^\s*#define\s+(\w+)\s+customParams\[(\d+)\]\.([xyzw]))"),
            QRegularExpression::MultilineOption);
        static const QRegularExpression defineColorPattern(
            QStringLiteral(R"(^\s*#define\s+(\w+)\s+customColors\[(\d+)\])"), QRegularExpression::MultilineOption);

        QSet<QString> definedInShader;

        // Match customParams.<sub>
        auto floatIt = definePattern.globalMatch(fragText);
        while (floatIt.hasNext()) {
            const auto m = floatIt.next();
            const QString id = m.captured(1);
            const int slot = m.captured(2).toInt();
            const QChar subChar = m.captured(3).at(0);
            const int sub = subChar == u'x' ? 0 : subChar == u'y' ? 1 : subChar == u'z' ? 2 : 3;
            QVERIFY2(expected.contains(id),
                     qPrintable(QStringLiteral("Pack %1: shader defines `%2` but metadata.json has no such parameter")
                                    .arg(packDir, id)));
            const auto& exp = expected[id];
            QCOMPARE(exp.type, QStringLiteral("params"));
            QVERIFY2(exp.slot == slot && exp.sub == sub,
                     qPrintable(QStringLiteral("Pack %1: param `%2` mapped to customParams[%3].%4 but metadata "
                                               "declaration order puts it at customParams[%5].%6")
                                    .arg(packDir, id)
                                    .arg(slot)
                                    .arg(QChar(subChar))
                                    .arg(exp.slot)
                                    .arg(QChar(QChar(u'x' + exp.sub)))));
            definedInShader.insert(id);
        }

        // Match customColors[N]
        auto colorIt = defineColorPattern.globalMatch(fragText);
        while (colorIt.hasNext()) {
            const auto m = colorIt.next();
            const QString id = m.captured(1);
            const int slot = m.captured(2).toInt();
            QVERIFY2(expected.contains(id),
                     qPrintable(QStringLiteral("Pack %1: shader defines `%2` but metadata.json has no such parameter")
                                    .arg(packDir, id)));
            const auto& exp = expected[id];
            QCOMPARE(exp.type, QStringLiteral("color"));
            QVERIFY2(exp.slot == slot,
                     qPrintable(QStringLiteral("Pack %1: param `%2` mapped to customColors[%3] but metadata "
                                               "declaration order puts it at customColors[%4]")
                                    .arg(packDir, id)
                                    .arg(slot)
                                    .arg(exp.slot)));
            definedInShader.insert(id);
        }

        // Every metadata-declared parameter that the runtime would
        // route into a slot MUST have a corresponding `#define` in the
        // shader, otherwise the value the registry computes is sent
        // into a uniform name the shader never references — silently
        // dropped. (We tolerate metadata params that the shader chose
        // to consume directly via `customParams[N].x` without a
        // `#define` macro — those are checked indirectly when other
        // params would have to displace into the same slot, which
        // would fail the slot-match assertions above.)
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
            if (!definedInShader.contains(it.key())) {
                qWarning().noquote() << "Pack" << packDir << "param" << it.key()
                                     << "declared in metadata but no `#define` in effect.frag — "
                                     << "verify the shader either consumes" << it.value().type
                                     << "slot directly or rename / drop the metadata entry.";
            }
        }
    }
};

QTEST_MAIN(TestAnimationShaderParamWiring)
#include "test_animation_shader_param_wiring.moc"
