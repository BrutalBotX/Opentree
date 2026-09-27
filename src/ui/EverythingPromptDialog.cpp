#include "ui/EverythingPromptDialog.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "integrations/EverythingClient.h"

namespace opentree {

EverythingPromptDialog::EverythingPromptDialog(const QString &installedExecutable,
                                               const QString &availabilityError, QWidget *parent)
    : QDialog(parent)
    , m_dontShowAgain(new QCheckBox(QStringLiteral("Don't show this again"), this))
{
    setWindowTitle(QStringLiteral("Speed Up Scans with Everything"));
    resize(560, 380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *heading = new QLabel(QStringLiteral("<b>OpenTree can use the Everything index</b>"), this);
    heading->setTextFormat(Qt::RichText);
    layout->addWidget(heading);

    auto *body = new QLabel(this);
    body->setWordWrap(true);
    body->setText(QStringLiteral(
        "Everything is a free file indexer by voidtools. While it is running, OpenTree builds its "
        "tree from the Everything index instead of walking the disk, which is much faster on large drives.\n\n"
        "Everything is not running right now, so OpenTree is using its built-in filesystem scan. "
        "That works everywhere, it is just slower."));
    layout->addWidget(body);

    if (!installedExecutable.isEmpty()) {
        auto *installed = new QLabel(this);
        installed->setWordWrap(true);
        installed->setText(QStringLiteral("<b>Everything is installed:</b><br>%1").arg(installedExecutable));
        installed->setTextFormat(Qt::RichText);
        layout->addWidget(installed);
    } else {
        auto *missing = new QLabel(this);
        missing->setWordWrap(true);
        missing->setText(QStringLiteral(
            "<b>Everything was not found on this PC.</b><br>"
            "Download it from voidtools (free), install it, and OpenTree will pick it up automatically."));
        missing->setTextFormat(Qt::RichText);
        layout->addWidget(missing);
    }

    if (!availabilityError.isEmpty()) {
        auto *details = new QLabel(this);
        details->setWordWrap(true);
        details->setText(QStringLiteral("<span style='color:#9aa4b2;'>Details: %1</span>").arg(availabilityError.toHtmlEscaped()));
        details->setTextFormat(Qt::RichText);
        layout->addWidget(details);
    }

    layout->addStretch(1);
    layout->addWidget(m_dontShowAgain);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *downloadButton = buttons->addButton(QStringLiteral("Download Everything..."), QDialogButtonBox::ActionRole);
    QPushButton *startButton = nullptr;
    if (!installedExecutable.isEmpty()) {
        startButton = buttons->addButton(QStringLiteral("Start Everything Now"), QDialogButtonBox::ActionRole);
    }
    QPushButton *continueButton = buttons->addButton(QStringLiteral("Continue Without It"), QDialogButtonBox::RejectRole);
    continueButton->setDefault(true);

    connect(downloadButton, &QPushButton::clicked, this, [this]() {
        m_downloadRequested = true;
        accept();
    });
    if (startButton) {
        connect(startButton, &QPushButton::clicked, this, [this]() {
            m_startRequested = true;
            accept();
        });
    }
    connect(continueButton, &QPushButton::clicked, this, &QDialog::reject);

    layout->addWidget(buttons);
}

bool EverythingPromptDialog::dontShowAgain() const
{
    return m_dontShowAgain->isChecked();
}

} // namespace opentree
