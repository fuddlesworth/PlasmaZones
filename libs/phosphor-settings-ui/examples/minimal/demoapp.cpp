// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "demoapp.h"

#include <QObject>
#include <QUrl>

#include "PhosphorSettingsUi/PageController.h"

#include "generalpage.h"

namespace PhosphorSettingsUiExamplesMinimal {

namespace {

/** Trivial about page with no staged state. Demonstrates a read-only
 *  page that the framework still routes to and shows in the sidebar. */
class AboutPage : public PhosphorSettingsUi::PageController
{
public:
    explicit AboutPage(QObject* parent = nullptr)
        : PhosphorSettingsUi::PageController(QStringLiteral("about"), parent)
    {
    }

    bool isDirty() const override { return false; }
    void apply() override {}
    void discard() override {}
};

} // namespace

DemoApp::DemoApp(QObject* parent)
    : PhosphorSettingsUi::ApplicationController(parent)
{
    auto* general = new GeneralPage(this);
    registerPage(general,
                 {},
                 QStringLiteral("General"),
                 QUrl(QStringLiteral("qrc:/qt/qml/org/phosphor/settings/ui/"
                                     "examples/minimal/qml/GeneralPage.qml")),
                 QStringLiteral("preferences-system-symbolic"));

    auto* about = new AboutPage(this);
    registerPage(about,
                 {},
                 QStringLiteral("About"),
                 QUrl(QStringLiteral("qrc:/qt/qml/org/phosphor/settings/ui/"
                                     "examples/minimal/qml/AboutPage.qml")),
                 QStringLiteral("help-about-symbolic"));

    setCurrentPageId(QStringLiteral("general"));
}

} // namespace PhosphorSettingsUiExamplesMinimal
