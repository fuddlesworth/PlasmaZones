// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorconfig_export.h>

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace PhosphorConfig {

/// One host of a settings API, as the static descriptor its package installs
/// at `<datadir>/phosphor-works/<api>-hosts/<id>.json` describes it:
///
///     { "Id": "plasmazones", "Name": "PlasmaZones", "BusName": "org.phosphor_works.Placement",
///       "Backend": "json", "ConfigPath": "$XDG_CONFIG_HOME/plasmazones/config.json",
///       "Desktops": ["KDE"] }
///
/// Several hosts can serve one API (the placement API is served by
/// PlasmaZones on KWin and by the Phosphor compositor), each keeping its
/// settings in its own file. A settings module reads the descriptors to find
/// the file to edit whether or not the host runs.
struct PHOSPHORCONFIG_EXPORT HostDescriptor
{
    QString id;
    QString name;
    QString busName;
    /// The PhosphorConfig backend the file uses. Only `json` is known.
    QString backend;
    /// The file, starting with `$XDG_CONFIG_HOME`, `$XDG_DATA_HOME`,
    /// `$XDG_STATE_HOME` or `$HOME`. See resolvedConfigPath().
    QString configPath;
    /// `XDG_CURRENT_DESKTOP` entries the host serves, compared ignoring case.
    QStringList desktops;

    /// Parse @p json. Returns nullopt and sets @p error when a field is
    /// missing or invalid.
    static std::optional<HostDescriptor> parse(const QJsonObject& json, QString* error);

    /// configPath with its leading variable expanded for this user. Empty
    /// when the variable is unknown, the result is not absolute, or it
    /// contains a `..` segment.
    [[nodiscard]] QString resolvedConfigPath() const;
};

/// What the last host of an API to start wrote about itself, or the choice
/// a user made between several hosts (pid 0).
struct PHOSPHORCONFIG_EXPORT HostRecord
{
    QString host;
    QString configPath;
    qint64 pid = 0;
    QDateTime written;
};

/// Which host's settings file a module edits, from HostDirectory::resolve().
struct PHOSPHORCONFIG_EXPORT HostResolution
{
    /// The host, or nullopt when none could be chosen.
    std::optional<HostDescriptor> host;
    /// The file to edit: the record's when the record chose the host,
    /// otherwise the descriptor's resolved path.
    QString configPath;
    /// Several descriptors match this session and nothing chose between
    /// them: ask the user, then writeRecord() the answer with pid 0.
    QList<HostDescriptor> candidates;
    /// Some process owns the API's bus name, so the host can be asked to
    /// reload after a write.
    bool running = false;
};

/// The descriptors and the host record of one settings API.
///
/// A running host calls writeRecord() at start. The record stays after it
/// stops, so a settings module knows which file this user last used.
class PHOSPHORCONFIG_EXPORT HostDirectory
{
public:
    /// @p api names the API, `placement` for the placement API.
    explicit HostDirectory(QString api);

    [[nodiscard]] QString api() const;

    /// Every valid descriptor in the `phosphor-works/<api>-hosts/`
    /// directories of the XDG data dirs, sorted by id. A per-user descriptor
    /// overrides a system one with the same id. Invalid ones are skipped with
    /// a warning.
    [[nodiscard]] QList<HostDescriptor> descriptors() const;

    /// `$XDG_STATE_HOME/phosphor-works/<api>-host.json`.
    [[nodiscard]] QString recordPath() const;
    [[nodiscard]] std::optional<HostRecord> record() const;
    /// Record host @p hostId, keeping its settings in @p configPath, as this
    /// user's host of the API. A running host passes its own pid at start; a
    /// user's choice between hosts passes 0 and the descriptor's
    /// resolvedConfigPath(). Refused when @p hostId is not a valid id or
    /// @p configPath is not absolute.
    bool writeRecord(const QString& hostId, const QString& configPath, qint64 pid) const;

    /// Choose a host, in this order:
    ///   1. the host the record names, when the record was written by the
    ///      process that owns the bus name now (@p busOwnerPid, 0 when the
    ///      name has no owner)
    ///   2. the record, when nothing owns the name
    ///   3. the one descriptor whose desktops match @p currentDesktops
    ///   4. none, with candidates listing every match, when several do
    /// A record naming a host with no descriptor is ignored.
    static HostResolution resolve(const QList<HostDescriptor>& descriptors, const std::optional<HostRecord>& record,
                                  qint64 busOwnerPid, const QStringList& currentDesktops);

    /// `XDG_CURRENT_DESKTOP`, split on `:`.
    static QStringList currentDesktops();

private:
    QString m_api;
};

} // namespace PhosphorConfig
