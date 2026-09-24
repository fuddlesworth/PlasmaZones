// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceWatcher.h>
#include <PhosphorTheme/AppearanceStore.h>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace PhosphorTheme {

AppearanceWatcher::AppearanceWatcher(QObject* parent)
    : AppearanceWatcher(
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
              + QStringLiteral("/phosphor-shell/appearance.json"),
          QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc"), parent)
{
}

AppearanceWatcher::AppearanceWatcher(const QString& appearancePath, const QString& kwinPath, QObject* parent)
    : QObject(parent)
    , m_appearancePath(appearancePath)
    , m_kwinPath(kwinPath)
{
    m_reload.setSingleShot(true);
    m_reload.setInterval(40);
    m_previewPoll.setInterval(200);
    connect(&m_reload, &QTimer::timeout, this, &AppearanceWatcher::reload);
    connect(&m_previewPoll, &QTimer::timeout, this, &AppearanceWatcher::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        m_reload.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        m_reload.start();
    });
    reload();
}

void AppearanceWatcher::watchPaths()
{
    QStringList paths;
    for (const auto& path : {m_appearancePath, m_kwinPath}) {
        const QFileInfo info(path);
        if (info.exists())
            paths.append(info.absoluteFilePath());
        QDir directory(info.absolutePath());
        while (!directory.exists()) {
            const QString parent = QFileInfo(directory.absolutePath()).absolutePath();
            if (parent == directory.absolutePath())
                break;
            directory.setPath(parent);
        }
        if (directory.exists())
            paths.append(directory.absolutePath());
    }
    paths.removeDuplicates();
    const auto watched = m_watcher.files() + m_watcher.directories();
    for (const auto& path : watched) {
        if (!paths.contains(path))
            m_watcher.removePath(path);
    }
    for (const auto& path : paths) {
        if (!m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
    }
}

void AppearanceWatcher::reload()
{
    watchPaths();
    bool preview = false;
    const auto next = AppearanceStore::effectiveValues(m_appearancePath, &preview);
    if (preview && !m_previewPoll.isActive())
        m_previewPoll.start();
    else if (!preview)
        m_previewPoll.stop();
    QSettings kwin(m_kwinPath, QSettings::IniFormat);
    const bool active = next.value(QStringLiteral("desktopStyle")).toBool()
        && kwin.value(QStringLiteral("org.kde.kdecoration2/library")).toString()
            == QLatin1String("org.phosphor.decoration");
    if (next == m_values && active == m_desktopStyleActive)
        return;
    m_values = next;
    m_desktopStyleActive = active;
    Q_EMIT changed();
}

}
