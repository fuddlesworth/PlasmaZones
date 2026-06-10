// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "demoapp.h"

#include <QObject>
#include <QUrl>

#include "PhosphorControl/PageController.h"

#include "generalpage.h"

namespace PhosphorControlExamplesMinimal {

// Trivial about page with no staged state. Demonstrates a read-only
// page that the framework still routes to and shows in the sidebar.
// Q_OBJECT is included so the demo mirrors the real-consumer pattern:
// any page that adds signals or Q_PROPERTY later needs the macro
// present, and copy-pasting from the example shouldn't teach a broken
// pattern. Lifted out of the anonymous namespace — moc-generated
// `qt_meta_stringdata` symbol names for anonymous-namespace classes
// can collide across TUs if multiple files declare a same-named class
// in their own anonymous namespaces. Naming the class with the demo's
// namespace prefix keeps the metaobject symbols unique.
class DemoAboutPage : public PhosphorControl::PageController
{
    Q_OBJECT
public:
    explicit DemoAboutPage(QObject* parent = nullptr)
        : PhosphorControl::PageController(QStringLiteral("about"), parent)
    {
    }

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }
};

namespace {
// QML module path constant — the example's qt_add_qml_module() call
// in CMakeLists.txt sets URI `org.phosphor.control.examples.minimal`
// with RESOURCE_PREFIX `/qt/qml`, so QML files end up under this qrc
// prefix. Centralising the prefix here means a future module rename only
// touches one line — and the registerPage() call sites read like the
// declarative URLs they are, not a stringly-typed copy-paste hazard.
//
// Out-of-tree consumers should prefer one of:
//   * Qt.resolvedUrl("./Page.qml") from QML (relative to the calling file)
//   * a `Q_PROPERTY(QUrl pageUrl ... CONSTANT)` on the controller, set
//     from QML so the path stays in QML where it lives
// — both forms keep the C++ side free of qrc layout knowledge.
inline QUrl moduleQmlUrl(QLatin1String file)
{
    return QUrl(QStringLiteral("qrc:/qt/qml/org/phosphor/control/examples/minimal/qml/") + file);
}
} // namespace

DemoApp::DemoApp(QObject* parent)
    : PhosphorControl::ApplicationController(parent)
{
    auto* general = new GeneralPage(this);
    registerPage(general, {}, QStringLiteral("General"), moduleQmlUrl(QLatin1String("GeneralPage.qml")),
                 QStringLiteral("preferences-system-symbolic"));

    auto* about = new DemoAboutPage(this);
    registerPage(about, {}, QStringLiteral("About"), moduleQmlUrl(QLatin1String("AboutPage.qml")),
                 QStringLiteral("help-about-symbolic"));

    setCurrentPageId(QStringLiteral("general"));
}

} // namespace PhosphorControlExamplesMinimal

// Moc-generated metaobject for the in-source AboutPage Q_OBJECT class.
#include "demoapp.moc"
