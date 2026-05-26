// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PhosphorTheme/MatugenRunner.h>

#include <QColor>
#include <QTest>
#include <QVariantMap>

using namespace PhosphorTheme;

// Matugen integration is exercised via parseMatugenJson with prebaked
// JSON fixtures. We do NOT spawn matugen in tests — that would either
// require matugen on PATH (CI flake) or a mock binary (more code than
// the parser itself).

class TestMatugenRunner : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parse_handlesWrappedDarkLight();
    void parse_handlesSingleModeColorsObject();
    void parse_handlesBareModeAtRoot();
    void parse_rejectsMalformed();
    void parse_ignoresNonStringValues();
};

void TestMatugenRunner::parse_handlesWrappedDarkLight()
{
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": {
            "dark":  { "primary": "#112233", "on_primary": "#FFFFFF" },
            "light": { "primary": "#AABBCC", "on_primary": "#000000" }
        }
    })";
    const auto dark = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(dark.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));

    const auto light = r.parseMatugenJson(payload, QStringLiteral("light"));
    QCOMPARE(light.value(QStringLiteral("primary")).value<QColor>(), QColor("#AABBCC"));
}

void TestMatugenRunner::parse_handlesSingleModeColorsObject()
{
    MatugenRunner r;
    // Older matugen schema: colors map directly under "colors", no
    // dark/light wrapper. Treat it as the requested mode.
    const QByteArray payload = R"({"colors": {"primary": "#112233"}})";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

void TestMatugenRunner::parse_handlesBareModeAtRoot()
{
    MatugenRunner r;
    const QByteArray payload = R"({"dark": {"primary": "#112233"}})";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

void TestMatugenRunner::parse_rejectsMalformed()
{
    MatugenRunner r;
    QVERIFY(r.parseMatugenJson("not json", QStringLiteral("dark")).isEmpty());
    QVERIFY(r.parseMatugenJson("[]", QStringLiteral("dark")).isEmpty());
}

void TestMatugenRunner::parse_ignoresNonStringValues()
{
    MatugenRunner r;
    const QByteArray payload = R"({
        "colors": { "dark": { "primary": "#112233", "version": 1, "junk": null } }
    })";
    const auto m = r.parseMatugenJson(payload, QStringLiteral("dark"));
    QCOMPARE(m.size(), 1);
    QCOMPARE(m.value(QStringLiteral("primary")).value<QColor>(), QColor("#112233"));
}

QTEST_MAIN(TestMatugenRunner)
#include "test_matugenrunner.moc"
