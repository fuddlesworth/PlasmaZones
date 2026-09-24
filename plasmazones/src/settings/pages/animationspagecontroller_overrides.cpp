// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Override-CRUD methods for AnimationsPageController. All methods here are
// members of the same class as animationspagecontroller.cpp — separate
// translation unit, no API change.
//
// Group covers:
//   * Path derivation (userProfilesDir / userMotionSetsDir)
//   * Existence + read (hasOverride / rawProfile / resolvedProfile)
//   * Write + clear (setOverride, and the clear family: clearOverride /
//     clearOverridesForPaths / clearAllOverrides / clearOverridesUnder)
//   * Scoped dirty check (hasScopedPendingOverrides)
//
// STORAGE. Every per-event TIMING override lives in one config key,
// `Animations/MotionProfileTree`, beside the pack assignment in
// `Animations/ShaderProfileTree`. Before schema v8 the timing half lived in
// loose `<data>/plasmazones/profiles/<event.path>.json` files, and that split
// is what this file used to be mostly about: a per-file pre-edit snapshot map,
// a disk-read memo in front of it, and an async worker to restore the files on
// Discard. None of it survived the move, because Settings already does all
// three for every other key — the committed baseline IS the snapshot, and
// Discard is `Settings::load()`. The decoration domain, whose surfaces were
// always config, never had any of it.
//
// Sibling _shaders.cpp owns the pack-tree side; the main TU owns the dirty
// check and the section catalog.

#include "animationspagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "animations_controller_detail.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonObject>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QVariantList>

namespace PlasmaZones {

using namespace animations_controller_detail;

QString AnimationsPageController::userProfilesDir() const
{
    // Still a real directory: it holds the user's SAVED PRESET library, which
    // is named by the user and has nothing to do with event paths. Per-event
    // overrides moved into config in schema v8.
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userProfilesSubdir();
}

QString AnimationsPageController::userMotionSetsDir() const
{
    // Test-mode layout: production places the preset library and the motion
    // sets at
    //   `<XDG>/plasmazones/profiles`
    //   `<XDG>/plasmazones/motionsets`
    // i.e. two SIBLING dirs under a shared `plasmazones` root. The
    // `userProfilesDirOverride()` test hook substitutes a single tmp root for
    // the profiles dir directly (no `/profiles` suffix), so we mirror that
    // one-level-up layout by returning `<override>/motionsets`.
    // The suffix is derived from the production accessor, which tracks a
    // rename of the LEAF segment only.
    if (!m_userProfilesDirOverride.isEmpty()) {
        const QString subdir = ConfigDefaults::userMotionSetsSubdir();
        return m_userProfilesDirOverride + subdir.mid(subdir.lastIndexOf(QLatin1Char('/')));
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userMotionSetsSubdir();
}

QVariantMap AnimationsPageController::motionTree() const
{
    if (m_settings == nullptr)
        return {};
    return m_settings->motionProfileTree();
}

void AnimationsPageController::writeMotionTree(const QVariantMap& tree)
{
    if (m_settings == nullptr)
        return;
    // Raised across the write so the nested motionProfileTreeChanged handler
    // can tell this apart from an external mover. Every writer here announces
    // its own paths, so the handler's card-wide broadcast would be redundant —
    // and, on the continuous paths, would defeat the per-card path filter once
    // per pointer move.
    ++m_selfTreeWriteDepth;
    const auto releaseDepth = qScopeGuard([this] {
        --m_selfTreeWriteDepth;
    });
    m_settings->setMotionProfileTree(tree);
}

bool AnimationsPageController::hasOverride(const QString& path) const
{
    if (!isValidEventPath(path))
        return false;
    return treeHasOverrideForPath(motionTree(), path);
}

QVariantMap AnimationsPageController::rawProfile(const QString& path) const
{
    if (!isValidEventPath(path))
        return {};
    // Sanitised, not raw. QML reads BOTH this and `resolvedProfile` for one
    // card: this one decides which fields the event owns (and so whether the
    // revert link shows), `resolvedProfile` supplies the number beside it.
    // Leaving it unsanitised made an override with a rejected field render as
    // "Overridden for this event", with a live revert link, next to the
    // INHERITED value the sanitizer had substituted — the card contradicting
    // itself on the one screen this controller exists to make honest.
    return sanitizedProfileMap(treeProfileForPath(motionTree(), path));
}

QVariantMap AnimationsPageController::resolvedProfile(const QString& path) const
{
    using namespace PhosphorAnimation;
    if (path.isEmpty())
        return {};

    QVariantMap merged;
    PhosphorProfileRegistry* registry = PhosphorProfileRegistry::defaultRegistry();
    // Read the tree ONCE for the whole walk. This backs a QML binding that
    // every card in scope re-evaluates on each overrideChanged, and setOverride
    // emits one of those per write path per slider tick, so a per-level read
    // would cost (visible cards x chain depth) store reads per tick of a
    // duration drag.
    const QVariantMap tree = motionTree();

    QString cur = path;
    while (!cur.isEmpty()) {
        QVariantMap source;
        if (isValidEventPath(cur)) {
            // The user's own overrides are read FIRST, ahead of the registry.
            // Both are fed from the same config key, but the registry is
            // repopulated from a `motionProfileTreeChanged` handler in the
            // composition root, and the PAGE re-reads synchronously inside the
            // `overrideChanged` handler this mutation is about to emit — so
            // registry-first can hand that read the pre-edit state. Config is
            // what this process just wrote, so config is the answer.
            source = sanitizedProfileMap(treeProfileForPath(tree, cur));
        }
        if (source.isEmpty() && registry != nullptr) {
            // No user override at this level: the registry supplies whatever
            // the hosting process registered and this controller cannot write —
            // the shell animation family seeds.
            const auto entry = registry->resolve(cur);
            if (entry.has_value()) {
                source = profileToVariantMap(*entry);
            }
        }
        mergeMissingFields(merged, source);
        cur = ProfilePaths::parentPath(cur);
    }

    // Seed the ROOT of the chain from the user's Global animation settings.
    // The daemon fills the same fields the same way at
    // kSettingsDrivenProfilePaths (animation_profiles.cpp), including on the
    // branch where a tree override owns the path. It additionally carries
    // presetName, which ISettings exposes no accessor for and which is
    // decorative. The walk above only sees the user's per-event overrides, so
    // without this the settings app resolves inheritance against library
    // defaults and every card reports the built-in 200 ms while the user's
    // Global card shows their real value — the two disagree on one screen.
    // Lowest precedence: mergeMissingFields only fills fields no ancestor
    // supplied, so a real override at any level still wins.
    if (m_settings != nullptr) {
        // Every Global field ISettings exposes; presetName is daemon-side only
        // and has no ISettings accessor. minDistance / sequenceMode /
        // staggerInterval are all user-editable on the Global card, and seeding
        // only duration+curve left the other three resolving to library
        // defaults here while the daemon animated with the user's value.
        using P = Profile;
        QVariantMap settingsGlobal;
        settingsGlobal.insert(QLatin1String(P::JsonFieldDuration), m_settings->animationDuration());
        settingsGlobal.insert(QLatin1String(P::JsonFieldMinDistance), m_settings->animationMinDistance());
        settingsGlobal.insert(QLatin1String(P::JsonFieldSequenceMode), m_settings->animationSequenceMode());
        settingsGlobal.insert(QLatin1String(P::JsonFieldStaggerInterval), m_settings->animationStaggerInterval());
        const QString curve = m_settings->animationEasingCurve();
        if (!curve.isEmpty()) {
            settingsGlobal.insert(QLatin1String(P::JsonFieldCurve), curve);
        }
        mergeMissingFields(merged, settingsGlobal);
    }

    fillLibraryDefaults(merged);
    return merged;
}

bool AnimationsPageController::setOverride(const QString& path, const QVariantMap& profileJson)
{
    // No `pendingChangesChanged` anywhere in this file. The dirty state is
    // value-based against the committed baseline, and the one thing that can
    // move it here is the stored tree — so the settings signal the write
    // itself raises (`motionProfileTreeChanged`, wired in the constructor) is
    // the single announcement, and it covers the writers this controller does
    // not own too: a profile apply, `Settings::load()` on Discard. Emitting
    // alongside it produced two signals per slider tick and one flip-gated
    // sample that could disagree with the other.
    const OverrideWrite result = writeOverrideOnly(path, profileJson);
    if (result == OverrideWrite::Failed)
        return false;
    if (result == OverrideWrite::Unchanged) {
        // Nothing moved, so there is nothing to announce and no re-read to do.
        return true;
    }
    Q_EMIT overrideChanged(path);
    return true;
}

AnimationsPageController::OverrideWrite AnimationsPageController::writeOverrideOnly(const QString& path,
                                                                                    const QVariantMap& profileJson)
{
    if (!isValidEventPath(path))
        return OverrideWrite::Failed;
    if (m_settings == nullptr) {
        qCWarning(lcConfig) << "writeOverrideOnly: no settings object; path=" << path;
        return OverrideWrite::Failed;
    }

    // The name field is not a Profile field — it was the per-file envelope's
    // way of naming the path, and the path is now the entry's own key. Strip it
    // so a caller round-tripping an old file's contents cannot smuggle it in.
    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.remove(JsonNameKey);

    const QVariantMap tree = motionTree();
    if (treeProfileForPath(tree, path) == obj) {
        // Round-trip with no real change — the caller emits nothing.
        return OverrideWrite::Unchanged;
    }
    writeMotionTree(treeWithOverrideForPath(tree, path, obj));
    return OverrideWrite::Written;
}

bool AnimationsPageController::writeOverridesBatch(const QList<QPair<QString, QVariantMap>>& edits)
{
    if (m_settings == nullptr) {
        qCWarning(lcConfig) << "writeOverridesBatch: no settings object";
        return false;
    }
    QVariantMap tree = motionTree();
    for (const auto& [path, profileJson] : edits) {
        // Same gate its single-path sibling applies. Both current callers
        // filter before they get here, so this is parity rather than a live
        // hole — but it is the only tree writer without it, and an entry at a
        // path outside the taxonomy is exactly the thing no page's scoped walk
        // can later see or remove.
        if (!isValidEventPath(path)) {
            qCWarning(lcConfig) << "writeOverridesBatch: skipping invalid event path" << path;
            continue;
        }
        QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
        obj.remove(JsonNameKey);
        tree = treeWithOverrideForPath(tree, path, obj);
    }
    writeMotionTree(tree);
    return true;
}

AnimationsPageController::OverrideRemoval AnimationsPageController::removeOverride(const QString& path)
{
    if (m_settings == nullptr)
        return OverrideRemoval::Failed;
    const QVariantMap tree = motionTree();
    if (!treeHasOverrideForPath(tree, path))
        return OverrideRemoval::Absent;
    writeMotionTree(treeWithOverrideForPath(tree, path, QJsonObject{}));
    return OverrideRemoval::Removed;
}

bool AnimationsPageController::clearOverride(const QString& path)
{
    if (!isValidEventPath(path))
        return false;
    if (!hasOverride(path))
        return false;
    const OverrideRemoval removal = removeOverride(path);
    // Absent means the override went away between the hasOverride() check a few
    // lines up and this call. The caller's intent is satisfied and nothing was
    // cleared here, so this still reports "nothing to clear" rather than
    // success — but the page's view of the path HAS changed, so it still gets
    // told.
    Q_EMIT overrideChanged(path);
    return removal == OverrideRemoval::Removed;
}

int AnimationsPageController::clearAllOverrides()
{
    // Clear every built-in event path. Paths with no override are skipped, so
    // only real overrides are removed.
    return clearOverridesForPaths(PhosphorAnimation::ProfilePaths::allBuiltInPaths(),
                                  QLatin1String("clearAllOverrides"));
}

int AnimationsPageController::clearOverridesForPaths(const QStringList& eventPaths, QLatin1String context)
{
    if (m_settings == nullptr) {
        qCWarning(lcConfig) << context << ": no settings object";
        return -1;
    }
    // ONE tree write for the whole batch. Writing per path would fire a
    // settings change signal per removal, and every one of those repopulates
    // the process's profile registry and re-evaluates every card binding.
    QVariantMap tree = motionTree();
    QStringList cleared;
    for (const QString& path : eventPaths) {
        if (!isRemovableEventPath(path))
            continue;
        if (!treeHasOverrideForPath(tree, path))
            continue; // nothing to clear here, which is most paths on a scoped reset
        tree = treeWithOverrideForPath(tree, path, QJsonObject{});
        cleared.append(path);
    }
    if (!cleared.isEmpty())
        writeMotionTree(tree);

    for (const QString& path : cleared)
        Q_EMIT overrideChanged(path);
    return int(cleared.size());
}

int AnimationsPageController::clearOverridesUnder(const QStringList& eventPaths)
{
    // Scoped clearAllOverrides, over the caller's own page subtree.
    return clearOverridesForPaths(eventPaths, QLatin1String("clearOverridesUnder"));
}

QStringList AnimationsPageController::storedTimingPaths() const
{
    if (m_settings == nullptr)
        return {};
    QStringList out = treeOverriddenPaths(m_settings->motionProfileTree());
    for (const QString& path : treeOverriddenPaths(m_settings->committedMotionProfileTree())) {
        if (!out.contains(path))
            out.append(path);
    }
    return out;
}

bool AnimationsPageController::hasScopedPendingOverrides(const QStringList& eventPaths) const
{
    // The timing half of a per-page dirty check: any in-scope path whose stored
    // override differs from the committed baseline. Same comparison the whole
    // page's dirty check makes, narrowed to a subtree.
    if (m_settings == nullptr)
        return false;
    const QVariantMap live = m_settings->motionProfileTree();
    const QVariantMap committed = m_settings->committedMotionProfileTree();
    for (const QString& path : eventPaths) {
        if (!isRemovableEventPath(path))
            continue;
        if (treeProfileForPath(live, path) != treeProfileForPath(committed, path))
            return true;
    }
    return false;
}

} // namespace PlasmaZones
