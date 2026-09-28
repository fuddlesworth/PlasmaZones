// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>
class QQmlEngine;
class QJSEngine;
namespace PhosphorTheme {
class AppearanceStore;
}
namespace PhosphorShellPicker {
// The wallpaper catalog and portable look recipes. Selection/decoding happens
// off the GUI thread; AppearanceStore owns the single Apply/Revert transaction.
class AppearanceLibrary : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QVariantMap imported READ imported NOTIFY importedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT)
public:
    AppearanceLibrary(PhosphorTheme::AppearanceStore* store, const QString& directory, QObject* parent = nullptr);
    static AppearanceLibrary* create(QQmlEngine*, QJSEngine*);
    QVariantList wallpapers() const
    {
        return m_wallpapers;
    }
    QVariantList presets() const;
    QVariantMap imported() const
    {
        return m_imported;
    }
    QString error() const
    {
        return m_error;
    }
    bool busy() const
    {
        return m_busy;
    }
    QStringList fonts() const;
    Q_INVOKABLE void rescan();
    Q_INVOKABLE QVariantMap wallpaper(const QString& path) const;
    Q_INVOKABLE void chooseWallpaper(const QString& path, const QString& screen, const QString& fit);
    Q_INVOKABLE void importImages(const QList<QUrl>& urls);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool savePreset(const QString& name, bool wallpaper, bool bar);
    Q_INVOKABLE bool removePreset(const QString& id);
    Q_INVOKABLE bool inspectImport(const QUrl& file);
    Q_INVOKABLE bool previewPreset(const QString& id, bool wallpaper, bool bar);
    Q_INVOKABLE bool saveImported(const QString& name);
    Q_INVOKABLE bool exportPreset(const QString& id, const QUrl& file);
    static QVariantMap inspectImage(const QString& path);
Q_SIGNALS:
    void wallpapersChanged();
    void presetsChanged();
    void importedChanged();
    void errorChanged();
    void busyChanged();

private:
    explicit AppearanceLibrary(QObject* parent = nullptr);
    bool fail(const QString& error);
    void setBusy(bool busy);
    bool writePresets(const QVariantList& presets);
    QVariantMap recipe(const QString& id) const;
    static QVariantMap normalizeRecipe(const QVariantMap& value);
    static QVariantMap scopedSettings(const QVariantMap& values, bool wallpaper, bool bar);
    QString uniqueName(const QString& name) const;
    void loadPresets();
    PhosphorTheme::AppearanceStore* m_store;
    QString m_directory;
    QVariantList m_wallpapers;
    QVariantList m_saved;
    QVariantMap m_imported;
    QString m_error;
    bool m_busy = false;
    quint64 m_generation = 0;
};
}
