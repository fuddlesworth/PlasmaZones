// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/WindowId.h>

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QtGlobal>

#include <array>
#include <cstddef>

/// The rows of the running-windows list the settings app's rule picker shows,
/// header-only so the rules are unit-testable without a compositor (same
/// pattern as gestureenddecisions.h). The stacking walk that feeds them is
/// PlasmaZonesEffect::slotRunningWindowsRequested.
namespace PlasmaZones::RunningWindowsDecisions {

/// One picker row. Each value a picker mode writes into a rule is spelled the
/// way the rule field it fills compares it, so a picked value matches the
/// window it was picked from.
struct Row
{
    /// The canonical class token (normalizeAppId of the class alone). The
    /// row's dedup key; empty for a classless window.
    QString windowClass;
    /// What the WindowClass rule field compares: KWin's
    /// "resourceName resourceClass" exactly as reported (the effect's rule
    /// query stamps the same w->windowClass()). Empty when that is blank,
    /// which KWin reports as a single space.
    QString rawWindowClass;
    /// Short name of the window's app id, the id the AppId rule field
    /// compares (normalizeAppId with the desktop file first). Non-empty
    /// whenever the window has a class or a desktop file.
    QString appName;
    /// What the Title rule field compares: KWin's caption including its
    /// suffix (the activation shortcut, and on X11 the host and duplicate
    /// marker), so a picked title matches this window as titled now.
    QString caption;
    QString desktopFile;
};

/// Build the row for a window reporting @p rawClass, @p caption and
/// @p desktopFile.
inline Row makeRow(const QString& rawClass, const QString& caption, const QString& desktopFile)
{
    Row row;
    row.windowClass = ::PhosphorIdentity::WindowId::normalizeAppId(QString(), rawClass);
    row.rawWindowClass = rawClass.trimmed().isEmpty() ? QString() : rawClass;
    const QString appId = ::PhosphorIdentity::WindowId::normalizeAppId(desktopFile, rawClass);
    const QString shortName = ::PhosphorIdentity::WindowId::deriveShortName(appId);
    row.appName = shortName.isEmpty() ? appId : shortName;
    row.caption = caption;
    row.desktopFile = desktopFile;
    return row;
}

/// The JSON object the effect sends for @p row, under the keys
/// RulePickerOptions::runningWindows reads in the settings app.
inline QJsonObject toJson(const Row& row)
{
    QJsonObject obj;
    // windowClass is the canonical token: the picker's Titles-mode subtext and
    // its app-name fallback read it, and an older settings build offers it to
    // Classes mode. rawWindowClass is what the WindowClass rule field compares.
    obj[QLatin1String("windowClass")] = row.windowClass;
    obj[QLatin1String("rawWindowClass")] = row.rawWindowClass;
    obj[QLatin1String("appName")] = row.appName;
    obj[QLatin1String("caption")] = row.caption;
    obj[QLatin1String("desktopFile")] = row.desktopFile;
    return obj;
}

/// Which rows the list keeps, fed top of the stack first.
///
/// A window with a class is listed once per class, keeping the topmost (the
/// most likely one the user means); the class-keyed modes offer one value per
/// class anyway, and Title mode then offers that window's title. A classless
/// window has no class to group by, so it is listed once per caption instead,
/// or once per desktop file when it has no caption, which still gives the
/// AppId and DesktopFile modes a value. One with neither offers no value to
/// any mode and is left out.
///
/// admits() and record() are separate so a row the payload budget refuses
/// does not use up its key, and a later, smaller row of the same group can
/// still stand in for it.
class RowFilter
{
public:
    /// True when @p row would be kept: it has a key and no recorded row
    /// shares it.
    bool admits(const Row& row) const
    {
        const KeySpace space = spaceOf(row);
        return space != KeySpace::None && !m_seen[slotOf(space)].contains(keyOf(row, space));
    }

    /// Record @p row as kept, so a later row with the same key is refused.
    void record(const Row& row)
    {
        const KeySpace space = spaceOf(row);
        if (space != KeySpace::None) {
            m_seen[slotOf(space)].insert(keyOf(row, space));
        }
    }

private:
    /// Each key space is its own set, so a caption that spells a class token
    /// or a desktop file does not shadow it.
    enum class KeySpace {
        None,
        Class,
        ClasslessCaption,
        ClasslessDesktopFile,
    };

    static KeySpace spaceOf(const Row& row)
    {
        if (!row.windowClass.isEmpty()) {
            return KeySpace::Class;
        }
        if (!row.caption.isEmpty()) {
            return KeySpace::ClasslessCaption;
        }
        return row.desktopFile.isEmpty() ? KeySpace::None : KeySpace::ClasslessDesktopFile;
    }

    static QString keyOf(const Row& row, KeySpace space)
    {
        switch (space) {
        case KeySpace::Class:
            return row.windowClass;
        case KeySpace::ClasslessCaption:
            return row.caption;
        case KeySpace::ClasslessDesktopFile:
            return row.desktopFile;
        case KeySpace::None:
            break;
        }
        return QString();
    }

    static std::size_t slotOf(KeySpace space)
    {
        return static_cast<std::size_t>(space) - 1;
    }

    std::array<QSet<QString>, 3> m_seen;
    // One set per key space after None, so a space added to the enum without
    // a set fails here instead of indexing past the array.
    static_assert(static_cast<std::size_t>(KeySpace::ClasslessDesktopFile) == std::tuple_size_v<decltype(m_seen)>,
                  "RowFilter::m_seen needs one set per KeySpace after None");
};

/// The byte budget of the serialized list (a compact JSON array), so the
/// effect never builds a payload larger than the daemon relays: a row that
/// would cross @p capBytes is left out, and smaller later rows still fit. The
/// budget counts the array brackets and the comma before every row but the
/// first, so fed each row's compact toJson() size it totals exactly the size
/// of the compact array of the rows it took.
class PayloadBudget
{
public:
    explicit PayloadBudget(qsizetype capBytes)
        : m_capBytes(capBytes)
    {
    }

    /// True when a row serializing to @p rowBytes fits, and spends it.
    bool take(qsizetype rowBytes)
    {
        const qsizetype need = rowBytes + (m_rows > 0 ? 1 : 0);
        if (m_usedBytes + need > m_capBytes) {
            return false;
        }
        m_usedBytes += need;
        ++m_rows;
        return true;
    }

private:
    qsizetype m_capBytes;
    qsizetype m_usedBytes = 2; // "[]"
    int m_rows = 0;
};

} // namespace PlasmaZones::RunningWindowsDecisions
