#pragma once

// Shared autocomplete helper for the address bars (Chart and Graph). Both panels feed the
// same kind of list: folder paths that are currently known from the scanned roots.

#include <QCompleter>
#include <QLineEdit>
#include <QSet>
#include <QStringList>
#include <QStringListModel>

namespace opentree::AddressBarAutocomplete {

constexpr int kMaxSuggestions = 20000;

// Updates (or installs) the completer on the address bar. The list is de-duplicated and
// capped so a whole-drive root cannot make the popup slow to open.
inline void apply(QLineEdit *addressBar, const QStringList &paths)
{
    if (!addressBar) {
        return;
    }

    QStringList unique;
    QSet<QString> seen;
    unique.reserve(qMin(paths.size(), kMaxSuggestions));
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const QString key = path.toLower();
        if (!seen.contains(key)) {
            seen.insert(key);
            unique << path;
        }
        if (unique.size() >= kMaxSuggestions) {
            break;
        }
    }

    auto *completer = addressBar->findChild<QCompleter *>(QStringLiteral("addressCompleter"));
    if (!completer) {
        completer = new QCompleter(addressBar);
        completer->setObjectName(QStringLiteral("addressCompleter"));
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setFilterMode(Qt::MatchContains);
        completer->setMaxVisibleItems(12);
        completer->setModel(new QStringListModel(completer));
        addressBar->setCompleter(completer);
    }

    // Reuse the model (setStringList) instead of replacing it, and skip the work when the
    // list did not change: every scan re-pushes the same folder list.
    auto *model = qobject_cast<QStringListModel *>(completer->model());
    if (!model) {
        model = new QStringListModel(completer);
        completer->setModel(model);
    }
    if (model->stringList() != unique) {
        model->setStringList(unique);
    }
}

} // namespace opentree::AddressBarAutocomplete
