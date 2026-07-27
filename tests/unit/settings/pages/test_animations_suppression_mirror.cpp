// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_suppression_mirror.cpp
 * @brief The stock-animation suppression mirror: `stockSuppressedEvents`
 *        (the settings-side twin of the compositor's
 *        syncStockEffectSuppression ownership gate) and its NOTIFY inputs.
 *
 * Split out of test_animations_page_controller.cpp, which had crossed the
 * repo's 1150-line hard ceiling; these slots are the file's one
 * self-contained concern with no fixture shared beyond the standard
 * controller setup. See that file's header for the companion-test map.
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// Author a minimal animation pack `<root>/<subdir>/metadata.json` plus an
/// effect.frag stub so the registry's scan accepts it (mirrors the
/// decoration-page test fixture). An empty @p appliesTo omits the field,
/// which loads the pack as universal (applies to every single-surface
/// class); a non-empty array constrains it to the listed event classes.
bool writeAnimationPack(const QString& root, const QString& subdir, const QJsonArray& appliesTo)
{
    const QString packDir = root + QLatin1Char('/') + subdir;
    if (!QDir().mkpath(packDir))
        return false;
    QJsonObject metadata{{QLatin1String("id"), subdir},
                         {QLatin1String("name"), subdir},
                         {QLatin1String("fragmentShader"), QStringLiteral("effect.frag")},
                         {QLatin1String("parameters"), QJsonArray{}}};
    if (!appliesTo.isEmpty())
        metadata.insert(QLatin1String("appliesTo"), appliesTo);
    QFile meta(packDir + QStringLiteral("/metadata.json"));
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate) || meta.write(QJsonDocument(metadata).toJson()) < 0)
        return false;
    QFile frag(packDir + QStringLiteral("/effect.frag"));
    return frag.open(QIODevice::WriteOnly | QIODevice::Truncate) && frag.write(QByteArrayLiteral("// stub\n")) > 0;
}

} // namespace

class TestAnimationsSuppressionMirror : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Stock-animation suppression mirror ───────────────────────────────

    /// `stockSuppressedEvents` is the settings-side mirror of the
    /// compositor's syncStockEffectSuppression ownership gate; the rule
    /// editor's stock-animation conflict chip hides for listed events. Pin
    /// the gate's three inputs: tree assignment (a tree-resolved effectId
    /// on the minimize/maximize path lists the event; with a null registry
    /// the unknown id gets the warm-up grace and counts as owned), the
    /// animations master toggle (off collapses the list to empty), and the
    /// NOTIFY wiring (both inputs fire stockSuppressedEventsChanged so the
    /// chip bindings re-evaluate).
    void stockSuppressedEvents_reflectsTreeAndMasterToggle()
    {
        IsolatedConfigGuard guard;
        Settings s;
        s.setAnimationsEnabled(true);
        AnimationsPageController c(nullptr, &s);

        // No tree assignment: nothing suppressed, no conflict-free events.
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        QSignalSpy spy(&c, &AnimationsPageController::stockSuppressedEventsChanged);

        // Tree-assign a minimize pack. The controller was built with a null
        // registry, so the effectId is unknown and the warm-up grace counts
        // it as owned (mirroring resolvedShaderProfile's grace).
        PhosphorAnimationShaders::ShaderProfileTree tree;
        PhosphorAnimationShaders::ShaderProfile profile;
        profile.effectId = QStringLiteral("genie");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);

        // `>=`, deliberately, unlike the tightened counts in
        // test_animations_shader_overrides.cpp. Three separate inputs feed the
        // stock-suppression gate (shaderProfileTreeChanged, effectsChanged,
        // animationsEnabledChanged), so one tree assignment may legitimately
        // notify more than once and the exact count is NOT part of the contract.
        // What matters is that the chip bindings are notified at all.
        QVERIFY2(spy.count() >= 1, "tree change must notify the chip bindings");
        // Minimize is owned; maximize (unassigned) is not.
        QCOMPARE(c.stockSuppressedEvents(), QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize});

        // Assigning the maximize path too lists both events.
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMaximize, profile);
        s.setShaderProfileTree(tree);
        QCOMPARE(c.stockSuppressedEvents(),
                 (QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize,
                              PhosphorAnimation::ProfilePaths::WindowMaximize}));

        // Master toggle off gates the whole predicate: nothing is owned and
        // the change is notified, so every chip comes back.
        const int beforeToggle = spy.count();
        s.setAnimationsEnabled(false);
        QVERIFY2(spy.count() > beforeToggle, "master-toggle flip must notify the chip bindings");
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        // Toggle back on: ownership returns.
        s.setAnimationsEnabled(true);
        QCOMPARE(c.stockSuppressedEvents(),
                 (QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize,
                              PhosphorAnimation::ProfilePaths::WindowMaximize}));
    }

    /// Registry-backed half of the gate: a KNOWN pack is owned only when its
    /// contract class applies to the event (the compositor refuses to run a
    /// mismatched pack, so the stock effect stays loaded and the conflict
    /// chip must show), and a committed registry rescan fires the third
    /// NOTIFY input so the chip bindings re-evaluate.
    void stockSuppressedEvents_registryContractGateAndNotify()
    {
        IsolatedConfigGuard guard;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Universal pack (no appliesTo) applies to the appearance-class
        // minimize path; the desktop-only pack is a two-texture contract
        // that never runs on a single-surface window event.
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("universal-pack"), {}));
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("desktop-pack"), QJsonArray{QStringLiteral("desktop")}));
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("universal-pack")));
        QVERIFY(registry.hasEffect(QStringLiteral("desktop-pack")));

        Settings s;
        s.setAnimationsEnabled(true);
        AnimationsPageController c(&registry, &s);
        QSignalSpy spy(&c, &AnimationsPageController::stockSuppressedEventsChanged);

        // Known, class-compatible pack: owned.
        PhosphorAnimationShaders::ShaderProfileTree tree;
        PhosphorAnimationShaders::ShaderProfile profile;
        profile.effectId = QStringLiteral("universal-pack");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);
        QCOMPARE(c.stockSuppressedEvents(), QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize});

        // Known, class-INCOMPATIBLE pack: not owned, chip stays visible.
        profile.effectId = QStringLiteral("desktop-pack");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        // Third NOTIFY input: a committed rescan re-fires the chip rebind
        // signal — but only when the rescan actually CHANGES the suppression
        // verdict (the NOTIFY is flip-gated, matching every other dirty
        // signal in the controller). The tree still points minimize at
        // desktop-pack (not owned, list empty); rewrite that pack as
        // UNIVERSAL so the rescan flips minimize to owned.
        const int beforeRescan = spy.count();
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("desktop-pack"), {}));
        registry.refresh();
        QTRY_VERIFY2(spy.count() > beforeRescan,
                     "a rescan that changes the suppression set must notify the chip bindings");
        QCOMPARE(c.stockSuppressedEvents(), QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize});
        // And the gate half: a rescan with an UNCHANGED verdict stays silent
        // (deleting the flip gate reddens this).
        const int afterFlip = spy.count();
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("unrelated-pack"), {}));
        registry.refresh();
        QTRY_VERIFY(registry.hasEffect(QStringLiteral("unrelated-pack")));
        QCOMPARE(spy.count(), afterFlip);
    }
};

QTEST_MAIN(TestAnimationsSuppressionMirror)
#include "test_animations_suppression_mirror.moc"
