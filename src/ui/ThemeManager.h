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
};

class ThemeManager {
public:
    static QMap<QString, ThemeDefinition> loadThemes(const QString &themesDirectory);
    static bool applyTheme(QApplication &app, const ThemeDefinition &theme);

private:
    static QMap<QString, ThemeDefinition> builtInThemes();
};

}
