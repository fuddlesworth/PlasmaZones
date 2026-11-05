// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_screeninfo_variantlist.cpp
 * @brief Coverage for @c screenInfoListToVariantList — specifically the
 *        pre-computed @c displayLabel and the connector-disambiguation
 *        branch (two identical panels become "… · DP-1" / "… · HDMI-A-1").
 *
 * The label builder has four interacting axes — virtual vs physical,
 * make/model present vs absent, connector present vs absent, and the
 * "connector already shown" skip — so each combination is locked here.
 * The skip is what prevents a redundant "DP-1 · DP-1" (physical, no
 * make/model, name == connector) and "VS1 — DP-2 · DP-2" (virtual, no
 * make/model); both are regression-guarded below.
 */

#include <PhosphorScreens/ScreenInfo.h>

#include <QTest>
#include <QVariantList>
#include <QVariantMap>

using namespace PhosphorScreens;

namespace {
QString labelAt(const QVariantList& list, int index)
{
    return list.at(index).toMap().value(QStringLiteral("displayLabel")).toString();
}
}

class TestScreenInfoVariantList : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void physical_makeModelResolution_appendsConnector()
    {
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("Ultra HD");
        s.width = 3840;
        s.height = 2160;
        s.connectorName = QStringLiteral("DP-1");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("LG Ultra HD (3840×2160) · DP-1"));
    }

    void physical_makeModelNoResolution_appendsConnector()
    {
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("Ultra HD");
        s.connectorName = QStringLiteral("DP-1");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("LG Ultra HD · DP-1"));
    }

    void physical_noMakeModel_nameEqualsConnector_skipsRedundantConnector()
    {
        // The regression the PR fixes: previously QML appended the connector
        // unconditionally, yielding "DP-1 · DP-1". The label IS the connector
        // here, so nothing is appended.
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.connectorName = QStringLiteral("DP-1");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("DP-1"));
    }

    void physical_noMakeModel_nameDiffersFromConnector_appendsConnector()
    {
        // No make/model, but the name is an EDID-derived id distinct from the
        // connector — the connector still disambiguates, so it is appended.
        ScreenInfo s;
        s.name = QStringLiteral("Dell:U2722D:115107");
        s.connectorName = QStringLiteral("DP-2");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("Dell:U2722D:115107 · DP-2"));
    }

    void physical_noConnector_noTrailingSeparator()
    {
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("Ultra HD");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("LG Ultra HD"));
    }

    void virtual_makeModel_appendsConnector()
    {
        // virtualIndex 0 → "VS1"; connector distinguishes the same virtual
        // screen on two different physical monitors.
        ScreenInfo s;
        s.name = QStringLiteral("Dell:U2722D:115107/vs:0");
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("Ultra HD");
        s.isVirtualScreen = true;
        s.virtualIndex = 0;
        s.connectorName = QStringLiteral("DP-1");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("VS1 — LG Ultra HD · DP-1"));
    }

    void virtual_noMakeModel_connectorIsMonitorName_skipsRedundantConnector()
    {
        // With no make/model the monitor-name slot falls back to the
        // connector, so the label reads "VS1 — DP-2"; appending "· DP-2"
        // again would be redundant and is skipped.
        ScreenInfo s;
        s.name = QStringLiteral("DP-2/vs:0");
        s.isVirtualScreen = true;
        s.virtualIndex = 0;
        s.connectorName = QStringLiteral("DP-2");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("VS1 — DP-2"));
    }

    void virtual_displayName_makeModel_appendsConnector()
    {
        ScreenInfo s;
        s.name = QStringLiteral("Dell:U2722D:115107/vs:1");
        s.manufacturer = QStringLiteral("LG");
        s.model = QStringLiteral("Ultra HD");
        s.isVirtualScreen = true;
        s.virtualIndex = 1;
        s.virtualDisplayName = QStringLiteral("Left");
        s.connectorName = QStringLiteral("DP-1");

        const QVariantList out = screenInfoListToVariantList({s});
        QCOMPARE(labelAt(out, 0), QStringLiteral("Left — LG Ultra HD · DP-1"));
    }

    void payloadKeys_alwaysEmitFlagsAndConnector()
    {
        ScreenInfo s;
        s.name = QStringLiteral("DP-1");
        s.isPrimary = true;
        s.connectorName = QStringLiteral("DP-1");

        const QVariantMap m = screenInfoListToVariantList({s}).at(0).toMap();
        // isVirtualScreen is always present (physical screens read `false`,
        // not `undefined`); isPrimary round-trips; connectorName is emitted
        // because it is non-empty.
        QVERIFY(m.contains(QStringLiteral("isVirtualScreen")));
        QCOMPARE(m.value(QStringLiteral("isVirtualScreen")).toBool(), false);
        QCOMPARE(m.value(QStringLiteral("isPrimary")).toBool(), true);
        QCOMPARE(m.value(QStringLiteral("connectorName")).toString(), QStringLiteral("DP-1"));
    }
};

QTEST_MAIN(TestScreenInfoVariantList)
#include "test_screeninfo_variantlist.moc"
