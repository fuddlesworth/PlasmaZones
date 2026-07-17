// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_decoration_tree.cpp
 * @brief Settings — DecorationProfileTree persistence + JSON facade.
 *
 * The decoration analogue of test_settings_shader_tree.cpp. The
 * per-surface decoration tree persists as one nested JSON entry under
 * Decorations/DecorationProfileTree, mirroring how the animation
 * shaderProfileTree persists under Animations/ShaderProfileTree. Pinned
 * behaviour:
 *   - a fresh Settings returns the EMPTY/neutral ConfigDefaults tree
 *     (no baseline chain, no overrides — border/titlebar visuals are
 *     rule-owned, not tree-owned)
 *   - setDecorationProfileTree round-trips a baseline chain + a leaf
 *     override across save/load and across a fresh Settings instance
 *     (the cross-process daemon path)
 *   - the setter's value-equality gate emits decorationProfileTreeChanged
 *     exactly once and stays silent on a same-tree write
 *   - the decorationProfileTreeJson facade returns compact JSON;
 *     setDecorationProfileTreeJson("") resets to the empty default and
 *     malformed JSON is ignored (no signal, tree unchanged)
 */

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// A baseline-plus-leaf tree used by several tests: baseline decorates
/// every window with the border+glow chain, while `window.tiled` narrows
/// to just glow.
PhosphorSurfaceShaders::DecorationProfileTree makeBaselinePlusLeafTree()
{
    PhosphorSurfaceShaders::DecorationProfileTree tree;

    PhosphorSurfaceShaders::DecorationProfile baseline;
    baseline.chain = QStringList{QStringLiteral("border"), QStringLiteral("glow")};
    tree.setBaseline(baseline);

    PhosphorSurfaceShaders::DecorationProfile leaf;
    leaf.chain = QStringList{QStringLiteral("glow")};
    tree.setOverride(QStringLiteral("window.tiled"), leaf);

    return tree;
}

} // namespace

class TestSettingsDecorationTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A fresh config has no Decorations/DecorationProfileTree entry, so
    /// Settings::decorationProfileTree() must fall back to the canonical
    /// ConfigDefaults default. The baseline stays neutral (no global chain —
    /// window/popup border and titlebar visuals are owned by the window
    /// rules), but the three PopupFrame card surfaces — "osd",
    /// "popup.layoutPicker", "popup.zoneSelector" — each ship the same default
    /// decoration chain (a crisp neutral frame-contrast border + a real,
    /// theme-tinted drop shadow) that chromes their cards through the
    /// surface-decoration pipeline. Snap-assist carries its own anchor, not
    /// PopupFrame, so it is left undecorated.
    void testDecorationProfileTree_defaultShipsCardChains()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = settings.decorationProfileTree();
        QCOMPARE(tree, ConfigDefaults::decorationProfileTree());
        QVERIFY2(!tree.baseline().chain.has_value(), "default baseline must carry no chain (fully neutral)");
        QCOMPARE(tree.overriddenPaths(),
                 (QStringList{QStringLiteral("osd"), QStringLiteral("popup.layoutPicker"),
                              QStringLiteral("popup.zoneSelector")}));

        // Every card surface resolves to the same border + theme-tinted shadow.
        const QStringList cardSurfaces{QStringLiteral("osd"), QStringLiteral("popup.layoutPicker"),
                                       QStringLiteral("popup.zoneSelector")};
        for (const QString& path : cardSurfaces) {
            const auto card = tree.resolve(path);
            QCOMPARE(card.enabledChain(), (QStringList{QStringLiteral("border"), QStringLiteral("shadow")}));
            const auto borderParams = card.effectiveParameters().value(QStringLiteral("border")).toMap();
            QVERIFY2(borderParams.value(QStringLiteral("useThemeNeutral")).toBool(),
                     qPrintable(QStringLiteral("%1 border must derive a neutral theme colour").arg(path)));
            QCOMPARE(borderParams.value(QStringLiteral("borderWidth")).toInt(), 1);
            const auto shadowParams = card.effectiveParameters().value(QStringLiteral("shadow")).toMap();
            QVERIFY2(shadowParams.value(QStringLiteral("useThemeTint")).toBool(),
                     qPrintable(QStringLiteral("%1 shadow must tint with the theme background").arg(path)));
        }

        // Snap-assist stays undecorated (no PopupFrame chrome to replace).
        QVERIFY2(tree.resolve(QStringLiteral("popup.snapAssist")).enabledChain().isEmpty(),
                 "snap-assist must not ship a default decoration");
    }

    /// The tree — a baseline chain plus a leaf override — must survive
    /// save() and reload through a fresh Settings instance. This is the
    /// cross-process path: the daemon's Settings reads the same disk file
    /// the settings app wrote. Regression guard mirroring the shader-tree
    /// round-trip (a missing Decorations-schema key would drop the blob at
    /// the Store gate and silently reset the picker).
    void testDecorationProfileTree_setRoundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;

        // Write through Settings A; verify the round-trip in the SAME
        // instance first.
        {
            Settings a;
            QSignalSpy spy(&a, &Settings::decorationProfileTreeChanged);
            a.setDecorationProfileTree(makeBaselinePlusLeafTree());
            QCOMPARE(spy.count(), 1);
            a.save();

            const auto reread = a.decorationProfileTree();
            QCOMPARE(reread.baseline().effectiveChain(),
                     (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));
            QVERIFY(reread.hasOverride(QStringLiteral("window.tiled")));
            QCOMPARE(reread.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                     QStringList{QStringLiteral("glow")});
        }

        // Open a fresh Settings on the same isolated config — the value
        // must survive the file load.
        {
            Settings b;
            const auto reread = b.decorationProfileTree();
            QVERIFY2(reread.hasOverride(QStringLiteral("window.tiled")),
                     "DecorationProfileTree must persist across Settings instances");
            QCOMPARE(reread.baseline().effectiveChain(),
                     (QStringList{QStringLiteral("border"), QStringLiteral("glow")}));
            QCOMPARE(reread.directOverride(QStringLiteral("window.tiled")).effectiveChain(),
                     QStringList{QStringLiteral("glow")});
        }
    }

    /// The setter's value-equality gate: a real change fires
    /// decorationProfileTreeChanged (and settingsChanged) exactly once,
    /// and writing the identical tree again is a no-op that fires
    /// nothing. Without the gate the QML two-way binding would re-dirty
    /// the page on every refresh.
    void testDecorationProfileTree_setterSignalsOnceAndGatesEqual()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy specificSpy(&settings, &Settings::decorationProfileTreeChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(specificSpy.isValid());
        QVERIFY(generalSpy.isValid());

        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);
        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);

        // Same tree again — value-equality gate must suppress the emit.
        settings.setDecorationProfileTree(tree);
        QCOMPARE(specificSpy.count(), 1);
    }

    /// decorationProfileTreeJson returns a compact serialization of the
    /// current tree that round-trips back through
    /// DecorationProfileTree::fromJson to an equal tree — this is the
    /// blob the QML two-way binding reads.
    void testDecorationProfileTreeJson_returnsCompactRoundTrippableJson()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);

        const QString json = settings.decorationProfileTreeJson();
        QVERIFY(!json.isEmpty());
        // Compact JSON carries no newline (indented JSON would).
        QVERIFY2(!json.contains(QLatin1Char('\n')), "decorationProfileTreeJson must be compact");

        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isObject());
        const auto parsed = PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object());
        QCOMPARE(parsed, tree);
    }

    /// setDecorationProfileTreeJson("") resets to the canonical default
    /// (the EMPTY/neutral tree), exactly like the animation
    /// shaderProfileTree facade. Clearing restores "no decoration".
    void testDecorationProfileTreeJson_emptyStringResetsToDefault()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setDecorationProfileTree(makeBaselinePlusLeafTree());
        QVERIFY(settings.decorationProfileTree().hasOverride(QStringLiteral("window.tiled")));

        settings.setDecorationProfileTreeJson(QString());
        QCOMPARE(settings.decorationProfileTree(), ConfigDefaults::decorationProfileTree());
        QVERIFY2(!settings.decorationProfileTree().hasOverride(QStringLiteral("window.tiled")),
                 "empty-string reset must drop every override");
    }

    /// Malformed JSON is ignored: the setter neither mutates the tree nor
    /// fires the changed signal, so a bad two-way-binding write can't
    /// clobber the user's decoration.
    void testDecorationProfileTreeJson_malformedIsIgnored()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto tree = makeBaselinePlusLeafTree();
        settings.setDecorationProfileTree(tree);

        QSignalSpy spy(&settings, &Settings::decorationProfileTreeChanged);
        settings.setDecorationProfileTreeJson(QStringLiteral("{ this is not valid json"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(settings.decorationProfileTree(), tree);
    }
};

QTEST_MAIN(TestSettingsDecorationTree)
#include "test_settings_decoration_tree.moc"
