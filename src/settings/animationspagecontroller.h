// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

/// Q_PROPERTY surface for the "Animations" settings page.
///
/// Edits per-event motion-profile overrides and surfaces the built-in
/// `PhosphorAnimation::ProfilePaths` taxonomy as a section-grouped list
/// for the QML drilldown.
///
/// ## Persistence model
///
/// Per-event overrides live as one Profile JSON file per path under
/// `~/.local/share/plasmazones/profiles/`. The daemon's existing
/// `PhosphorAnimation::ProfileLoader` watches that dir and pushes files
/// into `PhosphorProfileRegistry` automatically — no daemon code changes.
/// User-wins-over-shipped semantics are already wired by the loader's
/// owner-tag partitioning. The settings app has its own bootstrap-owned
/// loader (see `src/settings/main.cpp`); both watchers respond to the
/// same dir, so a write here updates QML thumbnails in-process AND
/// live-updates the daemon at runtime.
///
/// ## Effective-value resolution
///
/// `resolvedProfile()` walks the path's parent chain through the
/// process-wide `PhosphorProfileRegistry::defaultRegistry()` (covers
/// shipped + user overrides) with library-default fill-in. When the
/// registry isn't published (unit tests without bootstrap), it falls
/// back to walking the user dir directly.
class AnimationsPageController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal springOmegaMin READ springOmegaMin CONSTANT)
    Q_PROPERTY(qreal springOmegaMax READ springOmegaMax CONSTANT)
    Q_PROPERTY(qreal springZetaMin READ springZetaMin CONSTANT)
    Q_PROPERTY(qreal springZetaMax READ springZetaMax CONSTANT)

public:
    explicit AnimationsPageController(QObject* parent = nullptr);

    qreal springOmegaMin() const
    {
        return 0.1;
    }
    qreal springOmegaMax() const
    {
        return 200.0;
    }
    qreal springZetaMin() const
    {
        return 0.0;
    }
    qreal springZetaMax() const
    {
        return 10.0;
    }

    /// Built-in event paths, grouped by section. Each entry:
    /// ```
    /// { "section": "window", "label": "Window",
    ///   "paths": [ { "path": "window", "label": "Window (inherited)",
    ///                "parent": "global", "isCategory": true },
    ///              { "path": "window.open", "label": "Open",
    ///                "parent": "window", "isCategory": false }, ... ] }
    /// ```
    /// Reserved paths (`ProfilePaths::isReservedPath`) are excluded.
    Q_INVOKABLE QVariantList eventSections() const;

    /// First dotted segment of @p path, or `"global"` when @p path is the
    /// global root. Drives the sidebar grouping.
    Q_INVOKABLE QString sectionForPath(const QString& path) const;

    /// Title-cased label for @p path's last segment (e.g. `"zone.snapIn"`
    /// → `"Snap In"`). Falls back to the segment itself if humanisation
    /// fails. Translation hook lives in QML; this is the raw English
    /// form.
    Q_INVOKABLE QString eventLabel(const QString& path) const;

    /// Wraps `ProfilePaths::parentPath`.
    Q_INVOKABLE QString parentPath(const QString& path) const;

    /// Inheritance chain from @p path up to (but excluding) the empty
    /// root. Useful for "snap → zone → global" breadcrumbs.
    Q_INVOKABLE QStringList parentChain(const QString& path) const;

    /// True iff a user override file exists for @p path.
    Q_INVOKABLE bool hasOverride(const QString& path) const;

    /// Per-path override file content as a QVariantMap. Empty map when
    /// no override exists. The `name` field is stripped — callers care
    /// about the Profile fields only.
    Q_INVOKABLE QVariantMap rawProfile(const QString& path) const;

    /// Effective Profile for @p path: walks the parent chain through the
    /// process-wide registry (or user dir as fallback) and fills any
    /// still-missing fields with `Profile::Default*` constants. Always
    /// returns a populated map.
    Q_INVOKABLE QVariantMap resolvedProfile(const QString& path) const;

    /// Write @p profileJson as the user override at @p path. The map
    /// follows `Profile::toJson()` shape (curve / duration / minDistance /
    /// sequenceMode / staggerInterval / presetName); a top-level `name`
    /// field is added automatically. Emits `overrideChanged(path)` on
    /// success.
    /// @return true on a successful disk write.
    Q_INVOKABLE bool setOverride(const QString& path, const QVariantMap& profileJson);

    /// Delete the override file at @p path. Emits `overrideChanged(path)`
    /// when a file actually existed and was removed. @return true when a
    /// file was removed.
    Q_INVOKABLE bool clearOverride(const QString& path);

    /// Test hook: redirect file I/O to @p dir instead of the XDG default.
    /// Pass an empty string to restore the default. Not Q_INVOKABLE — QML
    /// callers must not redirect persistence.
    void setUserProfilesDirOverride(const QString& dir);

Q_SIGNALS:
    /// Emitted on any successful set/clearOverride. @p path is the
    /// affected event path.
    void overrideChanged(const QString& path);

private:
    QString userProfilesDir() const;
    QString profileFilePath(const QString& path) const;

    QString m_userProfilesDirOverride; ///< Empty = use XDG default
};

} // namespace PlasmaZones
