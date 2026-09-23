// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

namespace PhosphorTheme {

// The cross-process view of saved appearance or its live, locked preview.
// Native decorations, the compositor, and application adapters share this
// reader so an orphaned preview cannot become a permanent desktop style.
class PHOSPHORTHEME_EXPORT AppearanceWatcher : public QObject
{
    Q_OBJECT
public:
    explicit AppearanceWatcher(QObject* parent = nullptr);
    AppearanceWatcher(const QString& appearancePath, const QString& kwinPath, QObject* parent = nullptr);
    QVariantMap values() const
    {
        return m_values;
    }
    bool desktopStyleActive() const
    {
        return m_desktopStyleActive;
    }

Q_SIGNALS:
    void changed();

private:
    void reload();
    void watchPaths();
    QString m_appearancePath;
    QString m_kwinPath;
    QVariantMap m_values;
    bool m_desktopStyleActive = false;
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    QTimer m_previewPoll;
};

}
