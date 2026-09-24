// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>
#include <memory>

class QQmlEngine;
class QJSEngine;
class QLockFile;

namespace PhosphorTheme {
class PHOSPHORTHEME_EXPORT AppearanceStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY changed)
    Q_PROPERTY(QVariantMap palette READ palette NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
public:
    explicit AppearanceStore(const QString& path, QObject* parent = nullptr);
    ~AppearanceStore() override;
    QVariantMap values() const
    {
        return m_values;
    }
    QString error() const
    {
        return m_error;
    }
    // Process-owned: a geometry reload must not destroy a live preview.
    static AppearanceStore* create(QQmlEngine* engine, QJSEngine* scriptEngine);
    static QVariantMap defaults();
    static bool validate(const QVariantMap& values, QVariantMap& result);
    // Cross-process decoration readers use a draft only while its producer
    // holds the preview lock. A crashed editor cannot leave a saved-looking draft.
    static QVariantMap effectiveValues(const QString& path, bool* previewActive = nullptr);
    bool editing() const
    {
        return m_editing;
    }
    bool dirty() const
    {
        return m_editing && m_values != m_saved;
    }
    Q_INVOKABLE bool beginPreview();
    Q_INVOKABLE bool applyPreview();
    Q_INVOKABLE void revertPreview();
    Q_INVOKABLE void endPreview();
    Q_INVOKABLE bool setValues(const QVariantMap& values);
    Q_INVOKABLE bool setWallpaper(const QString& path, const QString& screen, const QString& fit);

    QString currentPreset() const;
    QVariantMap palette() const;
    Q_INVOKABLE QVariantMap paletteFor(const QVariantMap& settings) const;
    Q_INVOKABLE bool setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE bool moveWidget(const QString& id, const QString& region, int index = -1);
    Q_INVOKABLE bool resetBarLayout();
    Q_INVOKABLE bool applyPreset(const QString& preset);
    Q_INVOKABLE bool importPreset(const QUrl& file);
    Q_INVOKABLE bool exportPreset(const QUrl& file);
Q_SIGNALS:
    void changed();
    void geometryChanged();
    void errorChanged();

private:
    explicit AppearanceStore(QObject* parent = nullptr);
    static QVariantMap presetValues(const QString& preset);
    bool write(const QVariantMap& values);
    bool writeDocument(const QString& path, const QVariantMap& values);
    void publish(const QVariantMap& values);
    bool commit(const QVariantMap& values);
    bool fail(const QString& error);
    QString m_path;
    QString m_error;
    QVariantMap m_values;
    QVariantMap m_saved;
    bool m_editing = false;
    std::unique_ptr<QLockFile> m_previewLock;
};
}
