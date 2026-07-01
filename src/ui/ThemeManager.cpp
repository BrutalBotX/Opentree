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
                          const QColor &button, const QColor &highlight, const QColor &highlightedText)
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
    theme.palette = palette;
    theme.styleSheet = QStringLiteral(
        "QMenuBar { background: %1; color: %2; }"
        "QMenuBar::item { background: transparent; color: %2; padding: 6px 10px; }"
        "QMenuBar::item:selected { background: %3; color: %4; }"
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { color: %2; padding: 6px 20px; }"
        "QMenu::item:selected { background: %3; color: %4; }"
        "QLineEdit { background: %5; color: %2; border: 1px solid %3; padding: 6px 8px; selection-background-color: %3; selection-color: %4; }"
        "QLineEdit:focus { border: 1px solid %4; }"
        "QToolTip { color: %2; background-color: %5; border: 1px solid %3; }"
        "QTabWidget::pane { border: 1px solid %3; top: -1px; }"
        "QTabBar::tab { padding: 8px 12px; background: %5; color: %2; border: 1px solid %3; border-bottom: none; margin-right: 2px; }"
        "QTabBar::tab:selected { background: %1; color: %2; }"
        "QTabBar::tab:!selected { background: %6; color: %2; }"
        "QHeaderView::section { padding: 6px; background: %5; color: %2; border: 1px solid %3; }"
    ).arg(window.name(), text.name(), highlight.name(), highlightedText.name(), button.name(), base.name());
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
    themes.insert("dark", makeTheme("dark", "OpenTree Dark", "Built-in", QColor("#1f2430"), QColor("#252b39"), QColor("#e6e9ef"), QColor("#2b3242"), QColor("#4c84ff"), QColor("#ffffff")));
    themes.insert("light", makeTheme("light", "Light", "Built-in", QColor("#f3f4f6"), QColor("#ffffff"), QColor("#1f2937"), QColor("#e5e7eb"), QColor("#2563eb"), QColor("#ffffff")));
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
