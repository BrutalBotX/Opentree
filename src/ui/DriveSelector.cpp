#include "ui/DriveSelector.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStorageInfo>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

#include "utils/SizeFormatter.h"

namespace opentree {

namespace {

QString volumeLetter(const QStorageInfo &info)
{
    if (info.rootPath().size() > 1) {
        return info.rootPath().left(2);
    }
    return info.displayName();
}

class DriveMenuRow : public QWidget {
public:
    DriveMenuRow(const QString &title, const QString &detail, qint64 total, qint64 free, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 5, 10, 5);
        layout->setSpacing(3);

        auto *titleLabel = new QLabel(title, this);
        auto *detailLabel = new QLabel(detail, this);
        detailLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_bar = new CapacityBar(this);

        layout->addWidget(titleLabel);
        layout->addWidget(m_bar);
        layout->addWidget(detailLabel);
        m_bar->setValues(total, free);
    }

private:
    CapacityBar *m_bar = nullptr;
};

} // namespace

CapacityBar::CapacityBar(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(60);
}

void CapacityBar::setValues(qint64 total, qint64 free)
{
    m_total = std::max<qint64>(0, total);
    m_free = std::clamp<qint64>(free, 0, m_total);
    update();
}

QSize CapacityBar::sizeHint() const
{
    return QSize(160, 6);
}

int CapacityBar::heightForWidth(int) const
{
    return 6;
}

void CapacityBar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF track = QRectF(rect()).adjusted(0, 0, 0, 0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(70, 78, 96));
    painter.drawRoundedRect(track, 3, 3);

    if (m_total <= 0) {
        return;
    }

    const double usedRatio = double(m_total - m_free) / double(m_total);
    QRectF used = track;
    used.setWidth(track.width() * std::clamp(usedRatio, 0.0, 1.0));
    if (used.width() < 3.0) {
        used.setWidth(std::min(3.0, track.width()));
    }
    painter.setBrush(QColor(76, 132, 255));
    painter.drawRoundedRect(used, 3, 3);
}

DriveSelector::DriveSelector(QWidget *parent)
    : QWidget(parent)
    , m_label(new QLabel(this))
    , m_bar(new CapacityBar(this))
    , m_menu(new QMenu(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(2);
    m_label->setText(QStringLiteral("No drive"));
    m_label->setStyleSheet(QStringLiteral("color: palette(text);"));
    m_bar->setMinimumWidth(170);
    layout->addWidget(m_label);
    layout->addWidget(m_bar);

    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Click to pick a drive to scan"));
    refreshDisplay();
}

void DriveSelector::setRootPath(const QString &path)
{
    if (m_rootPath.compare(path, Qt::CaseInsensitive) == 0) {
        return;
    }
    m_rootPath = path;
    refreshDisplay();
}

QString DriveSelector::rootPath() const
{
    return m_rootPath;
}

void DriveSelector::refreshDisplay()
{
    if (m_rootPath.isEmpty()) {
        m_label->setText(QStringLiteral("No drive"));
        m_bar->setValues(0, 0);
        return;
    }

    const QStorageInfo info(m_rootPath);
    if (!info.isValid() || !info.isReady()) {
        m_label->setText(QDir(m_rootPath).rootPath());
        m_bar->setValues(0, 0);
        return;
    }

    const qint64 total = info.bytesTotal();
    const qint64 free = info.bytesFree();
    m_label->setText(QStringLiteral("%1  %2 free of %3")
                         .arg(volumeLetter(info), SizeFormatter::formatBytes(free), SizeFormatter::formatBytes(total)));
    m_bar->setValues(total, free);
}

void DriveSelector::showMenu()
{
    m_menu->clear();

    QVector<QStorageInfo> volumes;
    const QList<QStorageInfo> mounted = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &info : mounted) {
        if (!info.isValid() || !info.isReady() || info.rootPath().isEmpty()) {
            continue;
        }
        volumes.push_back(info);
    }
    std::sort(volumes.begin(), volumes.end(), [](const QStorageInfo &left, const QStorageInfo &right) {
        return left.rootPath().compare(right.rootPath(), Qt::CaseInsensitive) < 0;
    });

    for (const QStorageInfo &info : volumes) {
        const QString root = info.rootPath();
        const QString detail = QStringLiteral("%1 free of %2")
                                   .arg(SizeFormatter::formatBytes(info.bytesFree()), SizeFormatter::formatBytes(info.bytesTotal()));
        auto *row = new DriveMenuRow(QStringLiteral("%1  (%2)").arg(info.displayName(), volumeLetter(info)),
                                      detail, info.bytesTotal(), info.bytesFree(), m_menu);
        row->setMinimumWidth(240);

        QWidgetAction *action = new QWidgetAction(m_menu);
        action->setDefaultWidget(row);
        connect(action, &QAction::triggered, this, [this, root]() { emit driveActivated(root); });
        m_menu->addAction(action);
    }

    if (volumes.isEmpty()) {
        m_menu->addAction(QStringLiteral("No ready volumes found"))->setEnabled(false);
    }

    m_menu->popup(mapToGlobal(QPoint(0, height() + 2)));
}

void DriveSelector::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
    showMenu();
}

}

