// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

/**
 * Minimal QML i18n context that routes through Qt's translation system.
 *
 * Install on a QQmlEngine so QML expressions can call i18n()/i18nc()/
 * i18np() as bare-word functions:
 *
 *   engine.rootContext()->setContextObject(new LocalizedContext(&engine));
 *
 * The Q_INVOKABLE methods delegate to QCoreApplication::translate() with
 * a configurable translation context — by default the application name.
 *
 * Apps that already use KLocalizedContext (KF6CoreAddons) should prefer
 * that — it provides richer plural forms and shares its catalogue with
 * the broader KDE translation infrastructure. This class exists for
 * Qt-only consumers that don't want a KF6 dependency.
 */
class PHOSPHORSETTINGSUI_EXPORT LocalizedContext : public QObject
{
    Q_OBJECT
    Q_PROPERTY(
        QString translationContext READ translationContext WRITE setTranslationContext NOTIFY translationContextChanged)
    QML_NAMED_ELEMENT(LocalizedContext)

public:
    explicit LocalizedContext(QObject* parent = nullptr);
    ~LocalizedContext() override;

    /**
     * Returns the current translation context — the explicit setter value
     * if non-empty, otherwise the live QCoreApplication::applicationName().
     *
     * Note: because the empty-state getter reads applicationName() at
     * call time, the property's effective value can change without a
     * NOTIFY emit if `setApplicationName()` is called externally while
     * `m_context` is empty. This is intentional: consumers wanting to
     * track late-binding application-name changes get them for free.
     * Consumers wanting a stable value should call setTranslationContext()
     * explicitly once at startup.
     */
    QString translationContext() const;
    void setTranslationContext(const QString& ctx);

    Q_INVOKABLE QString i18n(const QString& text) const;
    Q_INVOKABLE QString i18nc(const QString& context, const QString& text) const;
    Q_INVOKABLE QString i18np(const QString& singular, const QString& plural, int n) const;
    Q_INVOKABLE QString i18ncp(const QString& context, const QString& singular, const QString& plural, int n) const;

Q_SIGNALS:
    void translationContextChanged();

private:
    QString m_context;
};

} // namespace PhosphorSettingsUi
