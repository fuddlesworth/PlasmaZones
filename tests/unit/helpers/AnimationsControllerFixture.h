// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/**
 * @file AnimationsControllerFixture.h
 * @brief Shared AnimationsPageController fixtures for the shader-side tests.
 *
 * Two TUs cover this controller's shader surface —
 * test_animations_shader_overrides.cpp (per-path writes, resolution, picker
 * contract, leaf isolation) and test_animations_shader_param_writes.cpp (the
 * group writers and the group readers). Both need the same setup, and the
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
#include <QVariantList>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include "config/settings.h"
#include "settings/pages/animationspagecontroller.h"
#include "helpers/IsolatedConfigGuard.h"

namespace PlasmaZones::TestHelpers {

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

/// The same, with the bundled pack tree scanned in.
///
/// Skip separately, via dataAvailable(), rather than from inside this
/// constructor: QSKIP returns from the function it expands in, so a QSKIP here
/// would abandon the constructor and leave the slot running against a
/// half-built fixture.
struct PopulatedControllerFixture
{
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

    AnimationsPageController c{&registry, &settings};
};

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

} // namespace PlasmaZones::TestHelpers

/// Guards a slot that needs the bundled packs. A macro because QSKIP has to
/// expand in the slot's own body to return from it.
#define SKIP_WITHOUT_BUNDLED_PACKS()                                                                                   \
    do {                                                                                                               \
        if (!PlasmaZones::TestHelpers::PopulatedControllerFixture::dataAvailable())                                    \
            QSKIP("data/animations not found — running outside source tree");                                          \
    } while (false)
