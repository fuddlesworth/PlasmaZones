// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// PageHost against a live ApplicationController: the page it shows follows
// the registry when a page's QML changes under it.

#include <PhosphorControl/ApplicationController.h>
#include <PhosphorControl/PageController.h>

#include <QCoreApplication>
#include <QFile>
#include <QQmlError>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorControl;

namespace {

class SimplePage : public PageController
{
public:
    using PageController::PageController;
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
        Q_EMIT applyResult(true, QString());
    }
    void discard() override
    {
        Q_EMIT discardResult(true, QString());
    }
};

// Loader items are visual children, which QObject::findChild does not see.
bool hasItem(QQuickItem* root, const QString& objectName)
{
    const QList<QQuickItem*> children = root->childItems();
    for (QQuickItem* child : children) {
        if (child->objectName() == objectName || hasItem(child, objectName)) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestPageHost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        for (const char* name : {"first", "second"}) {
            QFile page(m_dir.filePath(QString::fromLatin1(name) + QStringLiteral(".qml")));
            QVERIFY(page.open(QIODevice::WriteOnly));
            page.write(QByteArray("import QtQuick\nItem { objectName: \"") + name + "\" }\n");
        }
    }

    void showsTheNewQmlWhenTheCurrentPageIsBound()
    {
        ApplicationController controller;
        controller.registerPage(new SimplePage(QStringLiteral("p"), &controller), QString(), QStringLiteral("P"),
                                url(QStringLiteral("first")));
        controller.setCurrentPageId(QStringLiteral("p"));

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import org.phosphor.control\nPageHost {}\n", QUrl());
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{QStringLiteral("controller"), QVariant::fromValue<QObject*>(&controller)}}));
        QVERIFY2(created, qPrintable(component.errorString()));
        auto* const host = qobject_cast<QQuickItem*>(created.get());
        QVERIFY(host);
        QTRY_VERIFY(hasItem(host, QStringLiteral("first")));

        // A module that loads while its placeholder page is on screen binds
        // the real page into it.
        QVERIFY(controller.bindPage(QStringLiteral("p"), new SimplePage(QStringLiteral("p"), &controller),
                                    url(QStringLiteral("second"))));
        QTRY_VERIFY(hasItem(host, QStringLiteral("second")));
        QVERIFY(!hasItem(host, QStringLiteral("first")));
    }

    void theUnavailablePageBuildsWithoutWarnings()
    {
        QQmlEngine engine;
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&warnings](const QList<QQmlError>& list) {
            warnings.append(list);
        });
        QQmlComponent component(&engine);
        component.setData("import org.phosphor.control\nFacetUnavailablePage { width: 400; height: 300 }\n", QUrl());
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        QCoreApplication::processEvents();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.isEmpty() ? QString() : warnings.first().toString()));
    }

private:
    QUrl url(const QString& name) const
    {
        return QUrl::fromLocalFile(m_dir.filePath(name + QStringLiteral(".qml")));
    }

    QTemporaryDir m_dir;
};

QTEST_MAIN(TestPageHost)
#include "test_page_host.moc"
