// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_shader_tree.cpp
 * @brief Settings — ShaderProfileTree persistence + auto-assign master toggle.
 *
 * Pinned behaviour:
 *   - setShaderProfileTree round-trips through the JSON blob (regression:
 *     missing schema key dropped the override silently)
 *   - purgeStaleKeys preserves keys the schema declares even when written
 *     directly via the backend (out-of-band write path)
 *   - shaderProfileTreeChanged fires from load() when the on-disk value
 *     differs (feeds the daemon's overlayservice refresh)
 *   - Persistence prune drops only paths that are NOT ancestors of any
 *     consumed leaf
 *   - Read-side prune self-heals legacy configs without requiring a save
 *   - Repeated set/read cycles preserve the latest write
 *   - autoAssignAllLayouts master toggle round-trips and signals correctly
 */

#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsShaderTree : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Regression: setShaderProfileTree must actually persist.
    /// The Animations schema in settingsschema.cpp must declare
    /// `shaderProfileTreeKey`, otherwise PhosphorConfig::Store::write
    /// rejects the JSON blob and the override silently disappears
    /// (shader picker resets to "None" immediately after each selection
    /// because the next read returns the unchanged on-disk value).
    void testShaderProfileTree_setRoundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;

        // Write a one-override tree through Settings A; verify the
        // round-trip via shaderProfileTree() in the SAME instance.
        {
            Settings a;
            PhosphorAnimationShaders::ShaderProfileTree tree;
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = QStringLiteral("pixelate");
            tree.setOverride(QStringLiteral("osd.show"), profile);
            QSignalSpy spy(&a, &Settings::shaderProfileTreeChanged);
            a.setShaderProfileTree(tree);
            QCOMPARE(spy.count(), 1);

            const auto reread = a.shaderProfileTree();
            QVERIFY(reread.hasOverride(QStringLiteral("osd.show")));
            const auto entry = reread.directOverride(QStringLiteral("osd.show"));
            QVERIFY(entry.effectId.has_value());
            QCOMPARE(*entry.effectId, QStringLiteral("pixelate"));
        }

        // Open a fresh Settings on the same isolated config — the value
        // must survive the file load. This is the cross-process path:
        // the daemon's Settings reads the same disk file the settings
        // app wrote.
        {
            Settings b;
            const auto reread = b.shaderProfileTree();
            QVERIFY2(reread.hasOverride(QStringLiteral("osd.show")),
                     "ShaderProfileTree must persist across Settings instances");
            const auto entry = reread.directOverride(QStringLiteral("osd.show"));
            QVERIFY(entry.effectId.has_value());
            QCOMPARE(*entry.effectId, QStringLiteral("pixelate"));
        }
    }

    /// Regression: purgeStaleKeys must NOT delete the shaderProfileTree
    /// key when it's present on disk but was not written through the
    /// Store schema gate (e.g. an external tool or migration wrote it
    /// directly via the backend). The earlier survivesSaveCycle test
    /// only covered the "Store wrote it" path which is already exercised
    /// by setRoundTripsThroughDisk. This variant writes directly to the
    /// backend, then triggers Settings::save() and asserts the key
    /// survives the purgeStaleKeys phase.
    void testShaderProfileTree_purgeStaleKeysPreservesDirectBackendWrite()
    {
        IsolatedConfigGuard guard;

        // Use a path the daemon overlay service actually consumes —
        // `Settings::shaderProfileTree()` prunes overrides on
        // unsupported paths to prevent stale leaf entries from
        // shadowing user-intended parent overrides at runtime.
        const QString kPath = QStringLiteral("osd.show");

        // Step 1: write the shader-tree JSON directly via the backend,
        // bypassing PhosphorConfig::Store::write entirely. This mimics a
        // migration-time or out-of-band write the Store loop never sees.
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            PhosphorAnimationShaders::ShaderProfileTree tree;
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = QStringLiteral("pixelate");
            tree.setOverride(kPath, profile);
            const QString json = QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));
            animations->writeString(ConfigDefaults::shaderProfileTreeKey(), json);
        }

        // Step 2: open Settings, run save() — purgeStaleKeys runs and
        // must keep the schema-declared key, even though the value
        // wasn't written via the Store.
        {
            Settings a;
            a.save();
        }

        // Step 3: re-read and confirm the override survived the purge.
        {
            Settings b;
            const auto reread = b.shaderProfileTree();
            QVERIFY2(reread.hasOverride(kPath),
                     "purgeStaleKeys must preserve shaderProfileTree when written directly to backend");
            const auto entry = reread.directOverride(kPath);
            QVERIFY(entry.effectId.has_value());
            QCOMPARE(*entry.effectId, QStringLiteral("pixelate"));
        }
    }

    /// Verify the shaderProfileTreeJson Q_PROPERTY's NOTIFY actually
    /// fires from Settings::load() when the on-disk value changes. This
    /// is what feeds the daemon's overlayservice — without the emit,
    /// applyShaderProfilesToAnimator never runs and shaders never
    /// reach the SurfaceAnimator on Save → notifyReload.
    void testShaderProfileTreeChanged_firesOnLoadWhenValueDiffers()
    {
        IsolatedConfigGuard guard;

        // Use a daemon-consumed path; see purgeStaleKeysPreservesDirectBackendWrite
        // for why "panel" would be pruned.
        const QString kPath = QStringLiteral("osd.show");

        // Seed disk with one tree, then mutate via a separate Settings
        // instance writing the file out.
        {
            Settings a;
            PhosphorAnimationShaders::ShaderProfileTree tree;
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = QStringLiteral("pixelate");
            tree.setOverride(kPath, profile);
            a.setShaderProfileTree(tree);
            a.save();
        }

        // Open a fresh Settings; the Q_PROPERTY load loop should emit
        // shaderProfileTreeChanged when load() reads the new disk value.
        Settings b;
        QSignalSpy spy(&b, &Settings::shaderProfileTreeChanged);

        // Modify the file externally (simulating the daemon's view of
        // a settings-app write that happened after `b` constructed).
        {
            Settings c;
            PhosphorAnimationShaders::ShaderProfileTree tree;
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = QStringLiteral("dissolve");
            tree.setOverride(kPath, profile);
            c.setShaderProfileTree(tree);
            c.save();
        }

        b.load();
        QVERIFY2(spy.count() >= 1,
                 "Settings::load() must re-emit shaderProfileTreeChanged when the on-disk value differs");
        QCOMPARE(b.shaderProfileTree().directOverride(kPath).effectId.value(), QStringLiteral("dissolve"));
    }

    /// Persistence prune drops only paths that are NOT ancestors of a
    /// consumed leaf. Parents like `popup` ARE valid — the resolver
    /// walks them on the way to consumed leaves, so a shader override
    /// at a parent cascades to its descendants. Paths like
    /// `widget.fadeIn` / `panel.slideIn` are dead at runtime (the daemon
    /// never resolves through them) and must be pruned so they can't
    /// shadow user-intended siblings.
    void testShaderProfileTree_prunesUnsupportedPathsOnPersistence()
    {
        IsolatedConfigGuard guard;

        // Seed a tree mixing supported and unsupported paths.
        PhosphorAnimationShaders::ShaderProfileTree dirty;
        PhosphorAnimationShaders::ShaderProfile slide;
        slide.effectId = QStringLiteral("slide");
        // Consumed leaf — must survive.
        dirty.setOverride(QStringLiteral("osd.show"), slide);
        // Ancestor of every popup-family consumed leaf — must survive
        // (cascading inheritance is the whole point of a tree).
        PhosphorAnimationShaders::ShaderProfile dissolve;
        dissolve.effectId = QStringLiteral("dissolve");
        dirty.setOverride(QStringLiteral("popup"), dissolve);
        // Sibling under `panel` that is NOT an ancestor of any consumed
        // leaf — must be pruned (the daemon's resolver never walks
        // through `panel.slideIn`, so an entry here is runtime-dead).
        PhosphorAnimationShaders::ShaderProfile pixelate;
        pixelate.effectId = QStringLiteral("pixelate");
        dirty.setOverride(QStringLiteral("panel.slideIn"), pixelate);
        // Unsupported subtree (no consumed leaf below).
        dirty.setOverride(QStringLiteral("widget.fadeIn"), pixelate);

        // Write through Settings (write-side prune).
        {
            Settings a;
            a.setShaderProfileTree(dirty);
            a.save();
        }

        // Read back; ancestors-of-consumed-leaves must survive,
        // unrelated paths must be dropped.
        Settings b;
        const auto loaded = b.shaderProfileTree();
        QVERIFY2(loaded.hasOverride(QStringLiteral("osd.show")), "consumed leaf must survive the prune");
        QVERIFY2(loaded.hasOverride(QStringLiteral("popup")),
                 "ancestor of consumed leaves must survive (cascading inheritance is by design)");
        QVERIFY2(!loaded.hasOverride(QStringLiteral("panel.slideIn")),
                 "sibling that is not an ancestor of any consumed leaf must be pruned");
        QVERIFY2(!loaded.hasOverride(QStringLiteral("widget.fadeIn")), "unsupported subtree path must be pruned");
        QCOMPARE(loaded.directOverride(QStringLiteral("osd.show")).effectId.value(), QStringLiteral("slide"));
        QCOMPARE(loaded.directOverride(QStringLiteral("popup")).effectId.value(), QStringLiteral("dissolve"));
    }

    /// Self-healing read prune: a config written by an earlier app
    /// version (without the prune at write) must still surface as
    /// pruned to consumers, so the daemon's resolver can't see stale
    /// shadow entries even before the user triggers a save.
    void testShaderProfileTree_readPruneSelfHealsLegacyConfig()
    {
        IsolatedConfigGuard guard;

        // Bypass Settings::setShaderProfileTree (which prunes) by writing
        // a tree directly to the backend — mimics a config produced by
        // a pre-prune app version.
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            PhosphorAnimationShaders::ShaderProfileTree tree;
            PhosphorAnimationShaders::ShaderProfile pixelate;
            pixelate.effectId = QStringLiteral("pixelate");
            // Stale leaf at an unsupported subtree.
            tree.setOverride(QStringLiteral("editor.snapIn"), pixelate);
            // Supported leaf the user has set since.
            PhosphorAnimationShaders::ShaderProfile slide;
            slide.effectId = QStringLiteral("slide");
            tree.setOverride(QStringLiteral("osd.show"), slide);
            const QString json = QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));
            animations->writeString(ConfigDefaults::shaderProfileTreeKey(), json);
        }

        // Settings::shaderProfileTree() must return the pruned view.
        Settings s;
        const auto loaded = s.shaderProfileTree();
        QVERIFY2(!loaded.hasOverride(QStringLiteral("editor.snapIn")),
                 "read-side prune must drop overrides on unsupported paths even before the next save");
        QVERIFY2(loaded.hasOverride(QStringLiteral("osd.show")), "supported path must survive read-side prune");
    }

    void testShaderProfileTree_repeatedSetReadCycle()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString path = QStringLiteral("osd.show");

        for (int i = 0; i < 3; ++i) {
            const QString effectId = (i == 0) ? QStringLiteral("pixelate")
                : (i == 1)                    ? QStringLiteral("dissolve")
                                              : QStringLiteral("glitch");

            // Simulate AnimationsPageController::setShaderOverride flow
            PhosphorAnimationShaders::ShaderProfileTree tree = settings.shaderProfileTree();
            PhosphorAnimationShaders::ShaderProfile profile;
            profile.effectId = effectId;
            tree.setOverride(path, profile);
            settings.setShaderProfileTree(tree);

            // Simulate refreshShaderFromTree: re-read and resolve
            const auto reread = settings.shaderProfileTree();
            const auto resolved = reread.resolve(path);
            QVERIFY2(resolved.effectId.has_value(),
                     qPrintable(QStringLiteral("cycle %1: effectId not engaged").arg(i)));
            QCOMPARE(*resolved.effectId, effectId);
        }
    }

    /// Global "Auto-assign for all layouts" master toggle (#370): defaults
    /// to false to preserve per-layout-only behavior on upgrade, the
    /// setter emits the specific NOTIFY signal once per real change, and
    /// the value round-trips through save/reload via the WindowHandling
    /// group.
    void testAutoAssignAllLayouts_defaultSetterRoundtrip()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.autoAssignAllLayouts(), false);

        QSignalSpy specificSpy(&settings, &Settings::autoAssignAllLayoutsChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(specificSpy.isValid());
        QVERIFY(generalSpy.isValid());

        settings.setAutoAssignAllLayouts(true);
        QCOMPARE(settings.autoAssignAllLayouts(), true);
        QCOMPARE(specificSpy.count(), 1);
        QVERIFY(generalSpy.count() >= 1);

        // Idempotent: same value must not re-emit
        settings.setAutoAssignAllLayouts(true);
        QCOMPARE(specificSpy.count(), 1);

        settings.save();

        Settings reloaded;
        QCOMPARE(reloaded.autoAssignAllLayouts(), true);
    }
};

QTEST_MAIN(TestSettingsShaderTree)
#include "test_settings_shader_tree.moc"
