// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QString>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT FileView : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(bool exists READ exists NOTIFY existsChanged)

public:
    explicit FileView(QObject* parent = nullptr);
    ~FileView() override;

    [[nodiscard]] QString path() const;
    void setPath(const QString& path);

    [[nodiscard]] QString content() const;

    [[nodiscard]] int interval() const;
    void setInterval(int interval);

    [[nodiscard]] bool exists() const;

Q_SIGNALS:
    void pathChanged();
    void contentChanged();
    void intervalChanged();
    void existsChanged();

private Q_SLOTS:
    void readFile();
    void onFileChanged();
    void onDirectoryChanged();

private:
    void setupWatcher();
    void setupTimer();

    QString m_path;
    QString m_content;
    int m_interval = 0;
    bool m_exists = false;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace PhosphorShell
