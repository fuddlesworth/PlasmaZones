// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file AnimationsControllerFixture.h
 * @brief Shared AnimationsPageController fixtures for the animations tests.
 *
 * Eleven TUs now include this, covering both the shader and the timing sides —
 * test_animations_shader_overrides.cpp (per-path writes, resolution, picker
 * contract, leaf isolation) and test_animations_shader_param_writes.cpp (the
 * group writers and the group readers) are the two it was written for. They
 * need the same setup, and the
 * setup is the kind that goes wrong quietly: a slot that forgets
 * IsolatedConfigGuard writes the developer's real user config, and one that
 * forgets to populate the registry silently exercises the permissive
 * empty-registry path instead of the gate it meant to test.
 *
 * That already happened once inside a single file, where eight copies of the
 * populated-registry preamble had drifted into three variants. Sharing the
 * fixtures across the split keeps the second TU from re-growing the same
 * divergence.
 */

#include <QDir>
#include <QStringList>
#include <QTest>
#include <QVariant>
#include <QJsonObject>
#include <QVariantList>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include "config/settings.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/IsolatedConfigGuard.h"

namespace PlasmaZones::TestHelpers {

/// Write a RAW per-event timing override straight into
/// `Animations/MotionProfileTree`, bypassing the controller.
///
/// For fixtures that need a malformed or out-of-domain value in the store —
/// the shapes `setOverride` would normalise away — so the READ side's
/// sanitising can be exercised. This is what hand-writing a profile JSON file
/// used to do before those overrides became config.
inline void setRawMotionOverride(Settings& settings, const QString& path, const QJsonObject& profile)
{
    QVariantMap tree = settings.motionProfileTree();
    QVariantList overrides = tree.value(QStringLiteral("overrides")).toList();
    QVariantMap entry;
    entry.insert(QStringLiteral("path"), path);
    entry.insert(QStringLiteral("profile"), profile.toVariantMap());
    bool replaced = false;
    for (int i = 0; i < overrides.size(); ++i) {
        if (overrides.at(i).toMap().value(QStringLiteral("path")).toString() == path) {
            overrides[i] = entry;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        overrides.append(entry);
    }
    tree.insert(QStringLiteral("overrides"), overrides);
    settings.setMotionProfileTree(tree);
}

/// Read a per-event timing override back RAW, exactly as stored.
///
/// The controller's `rawProfile` sanitises on read, so it drops a bad key
/// whether or not the writer did — a bound asserted through it would pass with
/// the writer's allowlist deleted. This reads the store.
inline QJsonObject rawMotionOverride(const Settings& settings, const QString& path)
{
    const QVariantList overrides = settings.motionProfileTree().value(QStringLiteral("overrides")).toList();
    for (const QVariant& entry : overrides) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("path")).toString() == path) {
            return QJsonObject::fromVariantMap(map.value(QStringLiteral("profile")).toMap());
        }
    }
    return {};
}

/// Controller over an isolated config with an EMPTY shader registry.
///
/// Member order is the contract: the guard must be constructed before the
/// Settings it isolates and destroyed after it, which declaration order gives
/// in both directions. Slots unpack it with a structured binding so their
/// bodies keep the plain `c` / `registry` / `settings` names.
struct ControllerFixture
{
    IsolatedConfigGuard guard;
    Settings settings;
    PhosphorAnimationShaders::AnimationShaderRegistry registry;
    AnimationsPageController c{&registry, &settings};
};

/// Controller over an isolated config with NO shader registry.
///
/// For the TIMING side of the page, which does not touch the pack registry at
/// all. It still needs a Settings: since schema v8 every per-event timing
/// override is a config key (`Animations/MotionProfileTree`), so a controller
/// without one can read nothing and write nothing — the same as the decoration
/// page controller, which has always needed its settings object.
struct TimingControllerFixture
{
    IsolatedConfigGuard guard;
    Settings settings;
    AnimationsPageController c{nullptr, &settings};
};

#ifdef P_SOURCE_DIR
/// The same, with the bundled pack tree scanned in.
///
/// Skip separately, via dataAvailable(), rather than from inside this
/// constructor: QSKIP returns from the function it expands in, so a QSKIP here
/// would abandon the constructor and leave the slot running against a
/// half-built fixture.
struct PopulatedControllerFixture
{
    /// REQUIRES `P_SOURCE_DIR` to be defined by the including target. Every
    /// target that compiles a TU including this header has to carry
    /// `target_compile_definitions(... P_SOURCE_DIR="${CMAKE_SOURCE_DIR}")`;
    /// without it this is a bare preprocessor error rather than anything that
    /// points at the cause.
    static QString dataDir()
    {
        return QStringLiteral(P_SOURCE_DIR "/data/animations");
    }
    static bool dataAvailable()
    {
        return QDir(dataDir()).exists();
    }

    IsolatedConfigGuard guard;
    Settings settings;
    PhosphorAnimationShaders::AnimationShaderRegistry registry;

    PopulatedControllerFixture()
    {
        // LiveReload::Off makes the initial scan synchronous, so every slot can
        // read the registry on the next line.
        registry.addSearchPath(dataDir(), PhosphorFsLoader::LiveReload::Off);
    }

    // Declared AFTER the constructor textually but 4th by declaration order, so
    // the controller is built BEFORE the scan above runs. Safe, and not by
    // accident: the controller only stores the registry pointer and reads it
    // lazily at each use, and it connects to `effectsChanged` in its own
    // constructor, so it observes the scan. Anyone reordering these members, or
    // adding one that reads the registry eagerly, has to revisit that.

    AnimationsPageController c{&registry, &settings};
};

/// Whether a raw shader profile map stores an `effectId` AT ALL.
///
/// Key presence, not value equality, and every assertion about which state a
/// path is in has to go through it. `QVariantMap::value()` default-constructs
/// for a missing key, so `value("effectId").toString()` reads an ABSENT key and
/// an engaged-EMPTY one identically as "" — and those two are different states
/// (inheriting the ancestor's pack, versus explicitly refusing it). Several
/// assertions conflated them and could not fail as written.
///
/// Exact, because `shaderProfileToMap` inserts the key if and only if the
/// optional is engaged.
inline bool storesEffectId(const QVariantMap& raw)
{
    return raw.contains(QStringLiteral("effectId"));
}

/// The picker's offer for @p path, flattened to plain ids.
///
/// Was an identical lambda re-declared at the top of each picker-contract slot.
/// Same duplication the fixtures remove, and the same risk: several copies of a
/// projection is several places for it to drift from what
/// `availableShaderEffectsForPath` actually returns.
inline QStringList pickerIdsFor(const AnimationsPageController& c, const QString& path)
{
    QStringList ids;
    const QVariantList list = c.availableShaderEffectsForPath(path);
    ids.reserve(list.size());
    for (const QVariant& v : list)
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

#endif // P_SOURCE_DIR

} // namespace PlasmaZones::TestHelpers

/// Guards a slot that needs the bundled packs. A macro because QSKIP has to
/// expand in the slot's own body to return from it. Defined only where
/// `P_SOURCE_DIR` is, since the fixture it names is.
#ifdef P_SOURCE_DIR
#define PZ_SKIP_WITHOUT_BUNDLED_PACKS()                                                                                \
    do {                                                                                                               \
        if (!PlasmaZones::TestHelpers::PopulatedControllerFixture::dataAvailable())                                    \
            QSKIP("data/animations not found — running outside source tree");                                          \
    } while (false)
#endif // P_SOURCE_DIR
