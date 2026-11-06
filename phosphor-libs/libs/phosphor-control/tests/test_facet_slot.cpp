// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FacetSlot and FacetExtensionFrame against a stubbed extension provider, so
// the QML is exercised without loading any module.

#include <PhosphorControl/ApplicationController.h>
#include <PhosphorControl/PageController.h>

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
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

QVariantMap entry(const QString& id, const QString& name)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("moduleId"), QStringLiteral("extender")},
            {QStringLiteral("moduleName"), QStringLiteral("Extender")},
            {QStringLiteral("name"), name}};
}

// Repeater and Loader items are visual children, not QObject children, so
// QObject::findChild does not see them.
QList<QQuickItem*> descendants(QQuickItem* item)
{
    QList<QQuickItem*> all;
    const QList<QQuickItem*> children = item->childItems();
    for (QQuickItem* child : children) {
        all.append(child);
        all.append(descendants(child));
    }
    return all;
}

QQuickItem* findItem(QQuickItem* root, const QString& objectName)
{
    const QList<QQuickItem*> all = descendants(root);
    for (QQuickItem* item : all) {
        if (item->objectName() == objectName) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

class TestFacetSlot : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(QStringLiteral("non-existent property|TypeError")));
    }

    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QFile section(m_dir.filePath(QStringLiteral("Section.qml")));
        QVERIFY(section.open(QIODevice::WriteOnly));
        section.write(
            "import QtQuick\n"
            "Item { property QtObject domain; property QtObject settingsApp; implicitHeight: 10 }\n");
    }

    void showsEachSectionAndRefreshesForItsPage()
    {
        ApplicationController controller;
        controller.registerPage(new SimplePage(QStringLiteral("good:general"), &controller), QString(),
                                QStringLiteral("General"), QUrl());
        QObject domain;
        QVariantList listed;
        controller.setExtensionProvider([&listed](const QString& pageId, const QString& slot) {
            return pageId == QLatin1String("good:general") && slot == QLatin1String("top") ? listed : QVariantList();
        });

        QVariantMap ok = entry(QStringLiteral("extender:ok"), QStringLiteral("Ok"));
        ok.insert(QStringLiteral("qmlSource"), QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Section.qml"))));
        ok.insert(QStringLiteral("domain"), QVariant::fromValue<QObject*>(&domain));
        QVariantMap broken = entry(QStringLiteral("extender:broken"), QStringLiteral("Broken"));
        broken.insert(QStringLiteral("error"), QStringLiteral("it failed to load"));
        listed = {ok, broken};

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import org.phosphor.control\nFacetSlot {}\n", QUrl());
        std::unique_ptr<QObject> created(component.createWithInitialProperties(
            {{QStringLiteral("settingsApp"), QVariant::fromValue<QObject*>(&controller)},
             {QStringLiteral("pageId"), QStringLiteral("good:general")},
             {QStringLiteral("slot"), QStringLiteral("top")}}));
        QVERIFY2(created, qPrintable(component.errorString()));
        auto* const slot = qobject_cast<QQuickItem*>(created.get());
        QVERIFY(slot);
        QVERIFY(slot->property("visible").toBool());

        QQuickItem* const okFrame = findItem(slot, QStringLiteral("facet-extension:extender:ok"));
        QVERIFY(okFrame);
        QCOMPARE(okFrame->property("errorText").toString(), QString());
        QQuickItem* loaded = nullptr;
        const QList<QQuickItem*> inFrame = descendants(okFrame);
        for (QQuickItem* child : inFrame) {
            if (child->property("domain").value<QObject*>() == &domain) {
                loaded = child;
            }
        }
        QVERIFY2(loaded, "the section's QML was not loaded with its domain");
        QCOMPARE(loaded->property("settingsApp").value<QObject*>(), &controller);

        QQuickItem* const brokenFrame = findItem(slot, QStringLiteral("facet-extension:extender:broken"));
        QVERIFY(brokenFrame);
        QCOMPARE(brokenFrame->property("errorText").toString(), QStringLiteral("it failed to load"));

        // Another page changing does not touch this slot; its own page does.
        listed.clear();
        controller.notifyExtensionsChanged(QStringLiteral("other"));
        QCOMPARE(slot->property("extensions").toList().size(), 2);
        controller.notifyExtensionsChanged(QStringLiteral("good:general"));
        QCOMPARE(slot->property("extensions").toList().size(), 0);
        QVERIFY(!slot->property("visible").toBool());
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestFacetSlot)
#include "test_facet_slot.moc"
