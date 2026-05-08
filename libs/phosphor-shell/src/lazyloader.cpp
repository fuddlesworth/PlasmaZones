// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/LazyLoader.h>

#include <QLoggingCategory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlIncubator>

Q_LOGGING_CATEGORY(lcLazyLoader, "phosphorshell.lazyloader")

namespace PhosphorShell {

class LazyIncubator : public QQmlIncubator
{
public:
    explicit LazyIncubator(LazyLoader* loader)
        : QQmlIncubator(QQmlIncubator::Asynchronous)
        , m_loader(loader)
    {
    }

protected:
    void statusChanged(Status status) override
    {
        if (status == Ready) {
            m_loader->onIncubatorReady();
        }
    }

private:
    LazyLoader* m_loader;
};

LazyLoader::LazyLoader(QQuickItem* parent)
    : QQuickItem(parent)
{
    setVisible(false);
}

LazyLoader::~LazyLoader()
{
    unload();
    delete m_ownedComponent;
}

bool LazyLoader::active() const
{
    return m_active;
}

void LazyLoader::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();

    if (active) {
        startLoading();
    } else {
        unload();
    }
}

QQmlComponent* LazyLoader::sourceComponent() const
{
    return m_sourceComponent;
}

void LazyLoader::setSourceComponent(QQmlComponent* component)
{
    if (m_sourceComponent == component) {
        return;
    }
    m_sourceComponent = component;
    Q_EMIT sourceComponentChanged();

    if (m_active) {
        unload();
        startLoading();
    }
}

QUrl LazyLoader::source() const
{
    return m_source;
}

void LazyLoader::setSource(const QUrl& source)
{
    if (m_source == source) {
        return;
    }
    m_source = source;
    Q_EMIT sourceChanged();

    if (m_active) {
        unload();
        startLoading();
    }
}

QQuickItem* LazyLoader::item() const
{
    return m_item;
}

LazyLoader::Status LazyLoader::status() const
{
    return m_status;
}

void LazyLoader::startLoading()
{
    QQmlComponent* component = m_sourceComponent;

    if (!component && !m_source.isEmpty()) {
        auto* engine = qmlEngine(this);
        if (!engine) {
            return;
        }
        delete m_ownedComponent;
        m_ownedComponent = new QQmlComponent(engine, m_source, QQmlComponent::Asynchronous, this);
        component = m_ownedComponent;
    }

    if (!component) {
        return;
    }

    if (component->isError()) {
        m_status = Error;
        Q_EMIT statusChanged();
        qCWarning(lcLazyLoader) << "Component error:" << component->errorString();
        return;
    }

    m_status = Loading;
    Q_EMIT statusChanged();

    delete m_incubator;
    m_incubator = new LazyIncubator(this);

    auto* context = qmlContext(this);
    component->create(*m_incubator, context);
}

void LazyLoader::unload()
{
    delete m_incubator;
    m_incubator = nullptr;

    if (m_item) {
        delete m_item;
        m_item = nullptr;
        Q_EMIT itemChanged();
    }

    if (m_status != Null) {
        m_status = Null;
        Q_EMIT statusChanged();
    }
}

void LazyLoader::onIncubatorReady()
{
    if (!m_incubator) {
        return;
    }

    auto* obj = m_incubator->object();
    m_item = qobject_cast<QQuickItem*>(obj);

    if (m_item) {
        m_item->setParentItem(this);
        m_status = Ready;
        Q_EMIT itemChanged();
        Q_EMIT statusChanged();
        Q_EMIT loaded();
        qCDebug(lcLazyLoader) << "Loaded item";
    } else {
        delete obj;
        m_status = Error;
        Q_EMIT statusChanged();
        qCWarning(lcLazyLoader) << "Incubated object is not a QQuickItem";
    }

    delete m_incubator;
    m_incubator = nullptr;
}

} // namespace PhosphorShell
