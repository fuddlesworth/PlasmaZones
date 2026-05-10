// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlComponent;
class QQmlIncubator;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT LazyLoader : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(LazyLoader)

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(
        QQmlComponent* sourceComponent READ sourceComponent WRITE setSourceComponent NOTIFY sourceComponentChanged)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QQuickItem* item READ item NOTIFY itemChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)

public:
    enum Status {
        Null,
        Loading,
        Ready,
        Error
    };
    Q_ENUM(Status)

    explicit LazyLoader(QQuickItem* parent = nullptr);
    ~LazyLoader() override;

    [[nodiscard]] bool active() const;
    void setActive(bool active);

    [[nodiscard]] QQmlComponent* sourceComponent() const;
    void setSourceComponent(QQmlComponent* component);

    [[nodiscard]] QUrl source() const;
    void setSource(const QUrl& source);

    [[nodiscard]] QQuickItem* item() const;
    [[nodiscard]] Status status() const;

Q_SIGNALS:
    void activeChanged();
    void sourceComponentChanged();
    void sourceChanged();
    void itemChanged();
    void statusChanged();
    void loaded();

private:
    friend class LazyIncubator;

    void startLoading();
    void unload();
    void onIncubatorReady();

    bool m_active = false;
    QQmlComponent* m_sourceComponent = nullptr;
    QQmlComponent* m_ownedComponent = nullptr;
    QUrl m_source;
    QQuickItem* m_item = nullptr;
    QQmlIncubator* m_incubator = nullptr;
    Status m_status = Null;
};

} // namespace PhosphorShell
