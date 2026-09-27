#pragma once

#include <QMap>
#include <QPalette>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QApplication)

namespace opentree {

struct ThemeDefinition {
    QString id;
    QString name;
    QString author;
    QPalette palette;
    QString styleSheet;
    // How the graph draws folder nodes: "neutral" (plain discs) or "planets".
    QString graphStyle = QStringLiteral("neutral");
};

class ThemeManager {
public:
    static QMap<QString, ThemeDefinition> builtInThemes();
    static QMap<QString, ThemeDefinition> loadThemes(const QString &themesDirectory);
    static bool applyTheme(QApplication &app, const ThemeDefinition &theme);
};

}
