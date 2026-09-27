#include "ui/AboutDialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include "integrations/EverythingClient.h"

namespace opentree {

namespace {

QString buildDescription()
{
    // Compiler and architecture, so bug reports can mention them.
#if defined(Q_CC_MSVC)
    const QString compiler = QStringLiteral("MSVC");
#elif defined(Q_CC_GNU)
    const QString compiler = QStringLiteral("GCC %1").arg(QString::fromLatin1(__VERSION__));
#else
    const QString compiler = QStringLiteral("unknown compiler");
#endif
#if defined(Q_OS_WIN64)
    const QString arch = QStringLiteral("64-bit");
#else
    const QString arch = QStringLiteral("32-bit");
#endif
    return QStringLiteral("Qt %1, %2, %3").arg(QString::fromLatin1(qVersion()), compiler, arch);
}

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
    , m_updateLabel(new QLabel(this))
    , m_checkButton(new QPushButton(QStringLiteral("Check for Updates"), this))
    , m_downloadButton(new QPushButton(QStringLiteral("Open Download Page"), this))
    , m_releasesUrl(UpdateChecker::releasesPageUrl())
{
    setWindowTitle(QStringLiteral("About OpenTree"));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("OpenTree"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 6);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto *tagline = new QLabel(QStringLiteral("Find out where your disk space actually went."), this);
    tagline->setWordWrap(true);
    layout->addWidget(tagline);

    auto *blurb = new QLabel(this);
    blurb->setWordWrap(true);
    blurb->setText(QStringLiteral(
        "A disk usage explorer for Windows: scan a folder or a whole drive, browse the tree, "
        "compare snapshots over time, chase down duplicate and junk files, and export a report "
        "when you need to show someone the numbers.\n\n"
        "Made by BrutalBot, with Qt 6."));
    layout->addWidget(blurb);

    auto *details = new QLabel(this);
    details->setWordWrap(true);
    details->setText(QStringLiteral("<b>Version %1</b><br>%2<br>%3")
                         .arg(QApplication::applicationVersion(), buildDescription(),
                              UpdateChecker::repositoryUrl().toHtmlEscaped()));
    details->setTextFormat(Qt::RichText);
    layout->addWidget(details);

    m_updateLabel->setWordWrap(true);
    m_updateLabel->setText(QStringLiteral("Updates are checked against the GitHub releases page."));
    layout->addWidget(m_updateLabel);

    auto *updateRow = new QHBoxLayout;
    updateRow->setContentsMargins(0, 0, 0, 0);
    updateRow->addWidget(m_checkButton, 0);
    m_downloadButton->setVisible(false);
    updateRow->addWidget(m_downloadButton, 0);
    updateRow->addStretch(1);
    layout->addLayout(updateRow);

    auto *links = new QHBoxLayout;
    links->setContentsMargins(0, 0, 0, 0);
    auto *projectButton = new QPushButton(QStringLiteral("Project Page"), this);
    auto *everythingButton = new QPushButton(QStringLiteral("Get Everything"), this);
    links->addWidget(projectButton, 0);
    links->addWidget(everythingButton, 0);
    links->addStretch(1);
    layout->addLayout(links);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_checkButton, &QPushButton::clicked, this, &AboutDialog::checkForUpdates);
    connect(m_downloadButton, &QPushButton::clicked, this, &AboutDialog::openReleasesPage);
    connect(projectButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl(UpdateChecker::repositoryUrl()));
    });
    connect(everythingButton, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(EverythingClient::downloadUrl()));
    });
}

void AboutDialog::checkForUpdates()
{
    m_checkButton->setEnabled(false);
    m_updateLabel->setText(QStringLiteral("Checking GitHub..."));
    m_downloadButton->setVisible(false);

    auto *checker = new UpdateChecker(this);
    connect(checker, &UpdateChecker::finished, this, [this, checker](const UpdateCheckResult &result) {
        showResult(result);
        checker->deleteLater();
    });
    checker->check();
}

void AboutDialog::showResult(const UpdateCheckResult &result)
{
    m_checkButton->setEnabled(true);
    m_updateLabel->setText(result.message);

    if (result.status == UpdateCheckResult::Status::UpdateAvailable) {
        m_releasesUrl = result.releasesUrl.isEmpty() ? UpdateChecker::releasesPageUrl() : result.releasesUrl;
        m_downloadButton->setVisible(true);
        m_downloadButton->setDefault(true);
    } else if (result.status == UpdateCheckResult::Status::Failed) {
        m_releasesUrl = UpdateChecker::releasesPageUrl();
        m_downloadButton->setVisible(true);
    }
}

void AboutDialog::openReleasesPage()
{
    QDesktopServices::openUrl(QUrl(m_releasesUrl.isEmpty() ? UpdateChecker::releasesPageUrl() : m_releasesUrl));
}

} // namespace opentree
