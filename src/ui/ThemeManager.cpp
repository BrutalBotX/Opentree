#include "ui/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace opentree {

namespace {

QColor colorValue(const QJsonObject &object, const char *key, const QColor &fallback)
{
    return object.contains(key) ? QColor(object.value(key).toString()) : fallback;
}

ThemeDefinition makeTheme(const QString &id, const QString &name, const QString &author,
                          const QColor &window, const QColor &base, const QColor &text,
                          const QColor &button, const QColor &highlight, const QColor &highlightedText,
                          const QColor &border)
{
    ThemeDefinition theme;
    theme.id = id;
    theme.name = name;
    theme.author = author;
    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, window.lighter(110));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, highlightedText);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::BrightText, text);
    // Neutral structure colour, also read by the graph page for its chrome.
    palette.setColor(QPalette::Mid, border);
    theme.palette = palette;
    // Focus/hover accent: a brighter blue on dark themes and a deeper blue on light ones so
    // focused controls stay clearly outlined against the surrounding frame.
    const QColor accent = window.lightness() < 128 ? highlight.lighter(135) : highlight.darker(120);
    theme.styleSheet = QStringLiteral(
        // The menu bar is deliberately left to the palette: styling QMenuBar items makes Qt
        // drop clicks that arrive before the first popup is laid out, which left the menu
        // bar in a state where the drop-down would not open at all.
        "QMenu { background: %1; color: %2; border: 1px solid %8; }"
        "QMenu::item { color: %2; padding: 6px 20px; }"
        "QMenu::item:selected { background: %3; color: %4; }"
        "QDialog { background: %1; color: %2; }"
        "QDialog QLabel { color: %2; }"
        "QMessageBox { background: %1; color: %2; }"
        "QMessageBox QLabel { color: %2; }"
        "QGroupBox { color: %2; border: 1px solid %8; margin-top: 12px; padding-top: 8px; }"
        "QGroupBox::title { color: %2; subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        // One global rule for every push/tool button, so the accent frame appears in the
        // toolbar, panels, dialogs and message boxes alike.
        "QPushButton, QToolButton { background: %5; color: %2; border: 1px solid %8; padding: 6px 10px; }"
        "QPushButton:hover, QToolButton:hover { background: %3; color: %4; border: 1px solid %7; }"
        "QPushButton:pressed, QToolButton:pressed { background: %3; color: %4; }"
        "QPushButton:focus, QToolButton:focus { border: 1px solid %7; }"
        "QPushButton:disabled, QToolButton:disabled { color: rgba(255,255,255,0.45); border-color: %8; }"
        "QPushButton#destructiveButton { color: #FF8A80; border: 1px solid #B0392F; }"
        "QPushButton#destructiveButton:hover { background: #B0392F; color: #FFFFFF; border: 1px solid #FF8A80; }"
        "QPushButton#destructiveButton:focus { border: 1px solid #FF8A80; }"
        "QPushButton#destructiveButton:disabled { color: rgba(255,138,128,0.4); border-color: rgba(176,57,47,0.4); }"
        // Same destructive treatment for tool buttons (the details pane uses those).
        "QToolButton#destructiveButton { color: #FF8A80; border: 1px solid #B0392F; }"
        "QToolButton#destructiveButton:hover { background: #B0392F; color: #FFFFFF; border: 1px solid #FF8A80; }"
        "QToolButton#destructiveButton:focus { border: 1px solid #FF8A80; }"
        "QToolButton#destructiveButton:disabled { color: rgba(255,138,128,0.4); border-color: rgba(176,57,47,0.4); }"
        "QLineEdit { background: %5; color: %2; border: 1px solid %8; padding: 6px 8px; selection-background-color: %3; selection-color: %4; }"
        "QLineEdit:focus { border: 1px solid %7; }"
        "QComboBox, QSpinBox, QTimeEdit, QPlainTextEdit, QTextEdit { background: %5; color: %2; border: 1px solid %8; selection-background-color: %3; selection-color: %4; }"
        "QComboBox:focus, QSpinBox:focus, QTimeEdit:focus, QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid %7; }"
        "QPlainTextEdit, QTextEdit { padding: 4px 6px; }"
        "QComboBox { padding: 6px 24px 6px 8px; }"
        "QSpinBox, QTimeEdit { padding: 6px 20px 6px 8px; }"
        "QComboBox::drop-down { border-left: 1px solid %8; width: 22px; }"
        "QComboBox QAbstractItemView, QTableView, QTableWidget, QListWidget { background: %1; color: %2; selection-background-color: %3; selection-color: %4; }"
        "QCheckBox { color: %2; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid %8; border-radius: 4px; background: %5; }"
        "QCheckBox::indicator:hover { border: 1px solid %7; }"
        "QCheckBox::indicator:checked { background: %3; border: 1px solid %4; image: url(:/icons/check.png); }"
        "QCheckBox::indicator:disabled { border-color: %8; background: %6; }"
        "QToolTip { color: %2; background-color: %5; border: 1px solid %8; }"
        "QTabWidget::pane { border: 1px solid %8; top: -1px; }"
        "QTabBar::tab { padding: 8px 12px; background: %5; color: %2; border: 1px solid %8; border-bottom: none; margin-right: 2px; }"
        "QTabBar::tab:selected { background: %1; color: %2; }"
        "QTabBar::tab:!selected { background: %6; color: %2; }"
        "QHeaderView::section { padding: 6px 16px 6px 6px; background: %5; color: %2; border: 1px solid %8; }"
        "QHeaderView::section:hover { background: %3; color: %4; }"
        "QHeaderView::up-arrow { image: url(:/icons/arrow_up.png); width: 12px; height: 12px; subcontrol-position: center right; subcontrol-origin: padding; right: 4px; }"
        "QHeaderView::down-arrow { image: url(:/icons/arrow_down.png); width: 12px; height: 12px; subcontrol-position: center right; subcontrol-origin: padding; right: 4px; }"
        "QComboBox::down-arrow { image: url(:/icons/arrow_down.png); width: 12px; height: 12px; }"
        "QTableView, QTableWidget { background: %1; color: %2; gridline-color: %8; alternate-background-color: %6; }"
        "QTableView::item:selected, QTableWidget::item:selected { background: %3; color: %4; }"
        "QLabel { color: %2; }"
        "QProgressBar { background: %5; color: %2; border: 1px solid %8; border-radius: 5px; text-align: center; min-height: 16px; }"
        "QProgressBar::chunk { background: %3; border-radius: 4px; }"
        "QToolBar { background: %1; border: none; }"
        "QScrollBar:vertical { background: %1; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %8; min-height: 28px; border-radius: 6px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: %1; }"
        "QScrollBar:horizontal { background: %1; height: 12px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %8; min-width: 28px; border-radius: 6px; }"
        "QScrollBar::handle:horizontal:hover { background: %3; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: %1; }"
    ).arg(window.name(), text.name(), highlight.name(), highlightedText.name(), button.name(), base.name(),
          accent.name(), border.name());
    return theme;
}

ThemeDefinition themeFromJson(const QString &id, const QString &path, const ThemeDefinition &fallback)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fallback;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject paletteObject = root.value("palette").toObject();
    ThemeDefinition theme;
    theme.id = root.value("id").toString(id);
    theme.name = root.value("name").toString(id);
    theme.author = root.value("author").toString();
    theme.palette = fallback.palette;
    theme.palette.setColor(QPalette::Window, colorValue(paletteObject, "window", fallback.palette.color(QPalette::Window)));
    theme.palette.setColor(QPalette::WindowText, colorValue(paletteObject, "windowText", fallback.palette.color(QPalette::WindowText)));
    theme.palette.setColor(QPalette::Base, colorValue(paletteObject, "base", fallback.palette.color(QPalette::Base)));
    theme.palette.setColor(QPalette::Text, colorValue(paletteObject, "text", fallback.palette.color(QPalette::Text)));
    theme.palette.setColor(QPalette::Button, colorValue(paletteObject, "button", fallback.palette.color(QPalette::Button)));
    theme.palette.setColor(QPalette::ButtonText, colorValue(paletteObject, "buttonText", fallback.palette.color(QPalette::ButtonText)));
    theme.palette.setColor(QPalette::Highlight, colorValue(paletteObject, "highlight", fallback.palette.color(QPalette::Highlight)));
    theme.palette.setColor(QPalette::HighlightedText, colorValue(paletteObject, "highlightedText", fallback.palette.color(QPalette::HighlightedText)));

    QFile qssFile(QDir(QFileInfo(path).absolutePath()).filePath("theme.qss"));
    if (qssFile.open(QIODevice::ReadOnly)) {
        theme.styleSheet = QString::fromUtf8(qssFile.readAll());
    } else {
        theme.styleSheet = fallback.styleSheet;
    }

    return theme;
}

} // namespace

QMap<QString, ThemeDefinition> ThemeManager::builtInThemes()
{
    QMap<QString, ThemeDefinition> themes;
    // Neutral graphite palette for now: structure comes from the greys and colour is
    // reserved for selection/state, so the UI reads calmly. Richer themes come later.
    themes.insert("dark", makeTheme("dark", "OpenTree Dark", "Built-in",
                                    QColor("#1e1f22"), QColor("#26272b"), QColor("#e6e6e8"),
                                    QColor("#2f3034"), QColor("#4c5d73"), QColor("#ffffff"),
                                    QColor("#3a3c42")));
    themes.insert("light", makeTheme("light", "Light", "Built-in",
                                     QColor("#f4f4f5"), QColor("#ffffff"), QColor("#1f1f23"),
                                     QColor("#e7e7ea"), QColor("#5a6b80"), QColor("#ffffff"),
                                     QColor("#d4d4d8")));
    return themes;
}

QMap<QString, ThemeDefinition> ThemeManager::loadThemes(const QString &themesDirectory)
{
    QMap<QString, ThemeDefinition> themes = builtInThemes();
    if (themesDirectory.isEmpty()) {
        return themes;
    }

    QDir dir(themesDirectory);
    if (!dir.exists()) {
        dir.mkpath(".");
        return themes;
    }

    const QFileInfoList themeDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &themeDir : themeDirs) {
        const QString jsonPath = QDir(themeDir.absoluteFilePath()).filePath("theme.json");
        if (!QFile::exists(jsonPath)) {
            continue;
        }
        const QString id = themeDir.fileName();
        themes.insert(id, themeFromJson(id, jsonPath, themes.value(id, themes.value("dark"))));
    }

    return themes;
}

bool ThemeManager::applyTheme(QApplication &app, const ThemeDefinition &theme)
{
    app.setPalette(theme.palette);
    app.setStyleSheet(theme.styleSheet);
    return true;
}

}
