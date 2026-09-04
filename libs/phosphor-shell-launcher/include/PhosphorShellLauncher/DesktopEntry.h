// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorShellLauncher/phosphorshelllauncher_export.h>

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace PhosphorShellLauncher {

// One application from a freedesktop .desktop file: the fields the
// launcher needs to list it, match it and start it. Not a general
// key-file reader; the Desktop Entry group only, Type=Application only.
//
// Hand-parsed rather than through QSettings' IniFormat, which mangles
// exactly the parts of the spec a launcher relies on: it splits
// `;`-separated list values, drops localised `Name[de]` keys, and
// unescapes nothing. KService is not a dependency of this tree.
struct PHOSPHORSHELLLAUNCHER_EXPORT DesktopEntry
{
    // The desktop-file id per the spec: the path relative to the
    // applications directory with '/' replaced by '-' and the .desktop
    // suffix removed. "org.mozilla.firefox", "kde4-konsole".
    QString id;
    // Absolute path of the file this came from.
    QString filePath;
    // Name and the optional secondary lines, already localised for the
    // locale the parser was given.
    QString name;
    QString genericName;
    QString comment;
    // Freedesktop icon name, or an absolute path.
    QString icon;
    // The Exec line as written, field codes intact.
    QString exec;
    // Working directory for the process, if the entry asks for one.
    QString path;
    // Search terms the entry lists beyond its name.
    QStringList keywords;
    QStringList categories;
    // Runs in a terminal (Terminal=true).
    bool terminal = false;
    // NoDisplay / Hidden. Parsed and reported rather than rejected, so a
    // scanner can decide; the default scan drops both.
    bool noDisplay = false;
    bool hidden = false;
    // OnlyShowIn / NotShowIn desktop-environment lists, verbatim.
    QStringList onlyShowIn;
    QStringList notShowIn;

    // The Exec line turned into an argv the launcher can start: shell-style
    // quoting resolved per the spec, and the field codes (%f %u %F %U %i
    // %c %k and the rest) removed since a launcher passes no document.
    // `%%` becomes a literal `%`. Empty when Exec was empty.
    [[nodiscard]] QStringList execArgs() const;

    // Whether this entry should be shown on `currentDesktop` (the
    // `:`-separated XDG_CURRENT_DESKTOP list, e.g. {"KDE"}). Applies
    // OnlyShowIn then NotShowIn per the spec. An empty currentDesktop
    // shows everything that has no OnlyShowIn.
    [[nodiscard]] bool showsOn(const QStringList& currentDesktop) const;

    // Parse one file. `locale` picks the localised Name/GenericName/Comment
    // per the spec's fallback order ("de_DE" → "de" → unlocalised); pass
    // an empty locale for the unlocalised strings. Returns nullopt for a
    // file that is unreadable, has no [Desktop Entry] group, is not
    // Type=Application, has no Name, or names a TryExec that cannot be
    // found on PATH (the spec's "treat as if it did not exist").
    [[nodiscard]] static std::optional<DesktopEntry> parse(const QString& filePath, const QString& locale,
                                                           const QString& id = QString());
};

// Enumerates the applications a launcher should offer.
class PHOSPHORSHELLLAUNCHER_EXPORT DesktopEntryScanner
{
public:
    // Static-only. Instantiating it would suggest there is per-scanner
    // state, and there is none.
    DesktopEntryScanner() = delete;

    // The applications directories in precedence order: the user's own
    // first, then each XDG_DATA_DIRS entry. What QStandardPaths reports
    // for ApplicationsLocation.
    [[nodiscard]] static QStringList defaultDirectories();

    // Scan `directories` in order. The first directory to define an id
    // wins and later definitions of the same id are ignored, which is how
    // a user's ~/.local/share/applications override shadows a system
    // entry. Subdirectories are walked and folded into the id with '-'.
    //
    // Entries with NoDisplay or Hidden, and entries whose OnlyShowIn /
    // NotShowIn exclude `currentDesktop`, are dropped. Files that fail to
    // parse are skipped silently; a launcher should not refuse to list
    // anything because one packager's file is malformed.
    [[nodiscard]] static QList<DesktopEntry> scan(const QStringList& directories, const QString& locale,
                                                  const QStringList& currentDesktop);
};

} // namespace PhosphorShellLauncher
