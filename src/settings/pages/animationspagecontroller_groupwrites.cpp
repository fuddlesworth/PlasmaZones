// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// AnimationsPageController group-write methods: the per-field merge, the
// per-field clear, the shader-leg group mutators and the divergence measure
// that an event card applies across its whole write-path group (its own event
// path plus its declared mirrors).
//
// Same class as animationspagecontroller.cpp, separate TU, no API change.
// These were JS loops inside AnimationEventCard.qml until the write policy and
// `rawProfile`'s drop-versus-substitute rules had to be kept in step across two
// languages. See the "Group writes" block in the header for the full rationale
// and for what deliberately stayed in QML.

#include "animationspagecontroller.h"

#include "animations_controller_detail.h"
#include "core/platform/logging.h"

#include <PhosphorAnimation/Profile.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

namespace PlasmaZones {

namespace {

/// Canonical form of one path's comparable stored state, as a stable byte
/// string. Built from a QJsonObject rather than by concatenating values so key
/// ordering is the object's own (sorted) order and two paths holding the same
/// values always serialise identically.
QByteArray comparableStateKey(const QVariantMap& profile, const QVariantMap& shader, bool compareCurve)
{
    QJsonObject compared;
    const auto durationIt = profile.constFind(QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration));
    if (durationIt != profile.constEnd())
        compared.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration),
                        QJsonValue::fromVariant(durationIt.value()));
    if (compareCurve) {
        const auto curveIt = profile.constFind(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve));
        if (curveIt != profile.constEnd())
            compared.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve),
                            QJsonValue::fromVariant(curveIt.value()));
    }

    QJsonObject root;
    root.insert(QLatin1String("timing"), compared);
    root.insert(QLatin1String("shader"), QJsonObject::fromVariantMap(shader));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

bool AnimationsPageController::setOverrideMergedOnPaths(const QStringList& paths, const QVariantMap& fields,
                                                        const QVariant& curveFromCommit)
{
    // An invalid QVariant is QML's `undefined` arriving here, and it is the
    // signal for "the user did not touch the curve". Distinguished from a valid
    // empty string, which is a real (if unusual) authored value.
    const bool curveEdited = curveFromCommit.isValid() && !curveFromCommit.isNull();
    const QString editedCurve = curveEdited ? curveFromCommit.toString() : QString();

    bool allWritten = true;
    for (const QString& path : paths) {
        // Read through rawProfile rather than trusting a caller-supplied
        // snapshot: it is memoised on this side, and it normalises the file the
        // same way resolvedProfile does, so the merge below operates on exactly
        // the fields the daemon will honour.
        QVariantMap merged = rawProfile(path);
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            merged.insert(it.key(), it.value());

        // The curve is the one field with three outcomes rather than two.
        const QLatin1String curveKey(PhosphorAnimation::Profile::JsonFieldCurve);
        if (curveEdited) {
            merged.insert(curveKey, editedCurve);
        } else {
            // Keep this path's OWN curve when it has a real one, and otherwise
            // make sure no curve is written at all. The remove() matters: a
            // stored curve that is present-but-empty would survive the merge as
            // an engaged empty value, which BLOCKS inheritance instead of
            // allowing it, so the path would stop following its parent's curve
            // without the user ever asking for that.
            const QVariant own = merged.value(curveKey);
            if (own.metaType().id() != QMetaType::QString || own.toString().isEmpty())
                merged.remove(curveKey);
        }

        if (!setOverride(path, merged))
            allWritten = false;
    }
    return allWritten;
}

int AnimationsPageController::clearFieldOnPaths(const QStringList& paths, const QString& field)
{
    // Allowlisted rather than passed through to the JSON: this removes a key
    // from a file, and the only two fields a card's revert links own are the
    // timing pair. Anything else reaching here would be a caller bug, and
    // silently honouring it could strip a motion set's fields.
    if (field != QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)
        && field != QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration)) {
        qCWarning(lcConfig) << "clearFieldOnPaths: refusing to clear unrecognised field" << field;
        return 0;
    }

    int changed = 0;
    for (const QString& path : paths) {
        QVariantMap raw = rawProfile(path);
        if (!raw.contains(field))
            continue;
        raw.remove(field);
        // An override with nothing left in it is removed outright. An empty
        // file and no file resolve identically, but the card's toggle and the
        // pending-changes walk both key on the file existing.
        const bool ok = raw.isEmpty() ? clearOverride(path) : setOverride(path, raw);
        if (ok)
            ++changed;
    }
    return changed;
}

bool AnimationsPageController::anyPathSupportsShaderLeg(const QStringList& paths) const
{
    for (const QString& path : paths) {
        if (supportsShaderLeg(path))
            return true;
    }
    return false;
}

bool AnimationsPageController::allPathsHoldShaderEffect(const QStringList& paths, const QString& effectId) const
{
    for (const QString& path : paths) {
        const QVariantMap raw = rawShaderProfile(path);
        const auto it = raw.constFind(QLatin1String("effectId"));
        // Absent effectId means no direct override, which is never equal to a
        // stored one — not even to the engaged-empty sentinel.
        if (it == raw.constEnd() || it.value().metaType().id() != QMetaType::QString)
            return false;
        if (it.value().toString() != effectId)
            return false;
    }
    return true;
}

int AnimationsPageController::setShaderOverrideOnPaths(const QStringList& paths, const QString& effectId,
                                                       const QVariantMap& parameters)
{
    int written = 0;
    for (const QString& path : paths) {
        // Skipped, not attempted: setShaderOverride rejects a non-supporting
        // path anyway, and attempting it only adds a warning for a call that
        // could never land. The divergence measure omits the shader axis for
        // these same paths, so the two together keep the banner off for a
        // group mixing supporting and non-supporting paths.
        if (!supportsShaderLeg(path))
            continue;
        if (setShaderOverride(path, effectId, parameters))
            ++written;
    }
    return written;
}

int AnimationsPageController::clearShaderOverrideOnPaths(const QStringList& paths)
{
    int cleared = 0;
    for (const QString& path : paths) {
        if (clearShaderOverride(path))
            ++cleared;
    }
    return cleared;
}

int AnimationsPageController::clearShaderOverrideDescendantsOnPaths(const QStringList& paths)
{
    int cleared = 0;
    for (const QString& path : paths) {
        const int n = clearShaderOverrideDescendants(path);
        // Never summed in. A -1 folded into the total would be
        // indistinguishable from a smaller successful clear, and the caller
        // needs to tell a refusal from a no-op to decide whether to close its
        // editor.
        if (n < 0)
            return -1;
        cleared += n;
    }
    return cleared;
}

int AnimationsPageController::divergentPathCount(const QString& primaryPath, const QStringList& mirrorPaths,
                                                 bool compareCurve) const
{
    if (mirrorPaths.isEmpty())
        return 0;

    const auto keyFor = [this, compareCurve](const QString& path) {
        // The shader axis is compared only where a shader can actually be
        // stored. A non-supporting path holds nothing there permanently, so
        // comparing it against a supporting path's real leg would report a
        // divergence over an axis no control could converge.
        const QVariantMap shader = supportsShaderLeg(path) ? rawShaderProfile(path) : QVariantMap();
        return comparableStateKey(rawProfile(path), shader, compareCurve);
    };

    const QByteArray primary = keyFor(primaryPath);
    int diverged = 0;
    for (const QString& mirror : mirrorPaths) {
        if (keyFor(mirror) != primary)
            ++diverged;
    }
    // Plus one for the primary, which every diverging mirror differs FROM and
    // which the converging edit also rewrites. Zero when nothing diverges, so a
    // caller never renders a stale count.
    return diverged > 0 ? diverged + 1 : 0;
}

} // namespace PlasmaZones
