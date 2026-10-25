// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/WindowId.h>

#include <QSet>
#include <QString>
#include <QtGlobal>

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

/// Which rows the list keeps, fed top of the stack first.
///
/// A window with a class is listed once per class, keeping the topmost (the
/// most likely one the user means); the class-keyed modes offer one value per
/// class anyway, and Title mode then offers that window's title. A classless
/// window has no class to group by, so it is listed once per caption instead,
/// and one with no caption either offers no value to any mode and is left out.
class RowFilter
{
public:
    /// True when @p row is kept. Records it, so a later row with the same key
    /// is refused.
    bool admit(const Row& row)
    {
        if (row.windowClass.isEmpty()) {
            if (row.caption.isEmpty() || m_seenClasslessCaptions.contains(row.caption)) {
                return false;
            }
            m_seenClasslessCaptions.insert(row.caption);
            return true;
        }
        if (m_seenClasses.contains(row.windowClass)) {
            return false;
        }
        m_seenClasses.insert(row.windowClass);
        return true;
    }

private:
    QSet<QString> m_seenClasses;
    QSet<QString> m_seenClasslessCaptions;
};

/// The byte budget of the serialized list (a compact JSON array), so the
/// effect never builds a payload larger than the daemon relays: a row that
/// would cross @p capBytes is left out, and smaller later rows still fit. The
/// budget counts the array brackets and the comma before every row but the
/// first.
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
