// Core logic tests. These cover the non-UI pieces: path helpers, size formatting, duplicate
// detection, analysis, report generation and the snapshot ledger with its three-tier routing.
#include <QtTest>
#include <QDir>
#include <QMenu>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "domain/ScanTypes.h"
#include "services/AnalysisService.h"
#include "services/DedupService.h"
#include "services/ReportService.h"
#include "services/SnapshotService.h"
#include "integrations/EverythingClient.h"
#include "ui/EntryActions.h"
#include "ui/TableItems.h"
#include "ui/ThemeManager.h"
#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"

using namespace opentree;

namespace {

QSqlDatabase openSchemaDatabase(const QString &connectionName)
{
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(QStringLiteral(":memory:"));
    if (!database.open()) {
        return database;
    }

    QFile schema(QStringLiteral(":/sql/schema.sql"));
    if (!schema.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return database;
    }

    const QStringList statements = QString::fromUtf8(schema.readAll()).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &statement : statements) {
        const QString trimmed = statement.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        QSqlQuery query(database);
        query.exec(trimmed);
    }
    return database;
}

void writeFile(const QString &path, int size)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QByteArray(size, 'x'));
    }
}

FileEntry makeFile(const QString &path, qint64 size)
{
    FileEntry file;
    file.path = path;
    file.parentPath = PathUtils::parentPath(path);
    file.name = PathUtils::fileName(path);
    file.size = size;
    return file;
}

} // namespace

class TestPathUtils : public QObject {
    Q_OBJECT

private slots:
    void descendantMatching_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("root");
        QTest::addColumn<bool>("expected");

        QTest::newRow("drive root child") << "C:/Windows/System32" << "C:/" << true;
        QTest::newRow("drive root itself") << "C:/" << "C:/" << true;
        QTest::newRow("other drive") << "D:/Games" << "C:/" << false;
        QTest::newRow("same folder") << "C:/Windows" << "C:/Windows" << true;
        QTest::newRow("nested") << "C:/Windows/System32" << "C:/Windows" << true;
        QTest::newRow("prefix not boundary") << "C:/WindowsApps" << "C:/Windows" << false;
        QTest::newRow("trailing separator root") << "C:/Foo/Bar" << "C:/Foo/" << true;
        QTest::newRow("trailing separator equal") << "C:/Foo" << "C:/Foo/" << true;
        QTest::newRow("spaces") << "D:/old stuff/Others" << "D:/old stuff" << true;
        QTest::newRow("spaces prefix trap") << "D:/old stuffy" << "D:/old stuff" << false;
    }

    void descendantMatching()
    {
        QFETCH(QString, path);
        QFETCH(QString, root);
        QFETCH(bool, expected);
        QCOMPARE(PathUtils::isSameOrDescendant(path, root), expected);
    }

    void ancestorMatching()
    {
        QVERIFY(PathUtils::isAncestorOf("C:/Windows", "C:/Windows/System32/file.dll"));
        QVERIFY(PathUtils::isAncestorOf("C:/", "C:/Windows"));
        QVERIFY(!PathUtils::isAncestorOf("C:/Windows", "C:/WindowsApps"));
        // isAncestorOf is inclusive: a path is its own ancestor.
        QVERIFY(PathUtils::isAncestorOf("C:/Foo", "C:/Foo"));
        QVERIFY(!PathUtils::isAncestorOf("C:/Bar", "C:/Foo"));
    }

    void normalizeAndParent()
    {
        QCOMPARE(PathUtils::normalizePath("C:\\Windows\\System32\\"), QStringLiteral("C:/Windows/System32"));
        QCOMPARE(PathUtils::normalizePath("C:\\"), QStringLiteral("C:/"));
        QCOMPARE(PathUtils::parentPath("C:/Windows/System32"), QStringLiteral("C:/Windows"));
        QCOMPARE(PathUtils::fileName("C:/Windows/System32/kernel32.dll"), QStringLiteral("kernel32.dll"));
    }
};

class TestSizeFormatter : public QObject {
    Q_OBJECT

private slots:
    void formatsAdaptiveUnits()
    {
        QCOMPARE(SizeFormatter::formatBytes(512), QStringLiteral("512 B"));
        QCOMPARE(SizeFormatter::formatBytes(2048), QStringLiteral("2.00 KB"));
        QCOMPARE(SizeFormatter::formatBytes(5 * 1024 * 1024), QStringLiteral("5.00 MB"));
        QCOMPARE(SizeFormatter::formatBytes(3LL * 1024 * 1024 * 1024), QStringLiteral("3.00 GB"));
    }
};

class TestDedup : public QObject {
    Q_OBJECT

private slots:
    void systemPathsAreExcluded()
    {
        QVERIFY(isSystemScanExcluded("C:/Windows/System32/kernel32.dll"));
        QVERIFY(isSystemScanExcluded("C:/Program Files/App/app.exe"));
        QVERIFY(isSystemScanExcluded("C:/ProgramData/Microsoft/foo.dat"));
        QVERIFY(isSystemScanExcluded("C:/$Recycle.Bin/S-1-5-18/file"));
        QVERIFY(isSystemScanExcluded("C:/System Volume Information/tracking.log"));
        QVERIFY(isSystemScanExcluded("C:/Windows/WinSxS/backup.dll"));
        QVERIFY(isSystemScanExcluded("C:/Users/me/AppData/Local/Packages/App/file"));
        QVERIFY(!isSystemScanExcluded("C:/Users/me/Documents/report.docx"));
        // A user folder that merely contains "Windows" in its name must still be scanned.
        QVERIFY(!isSystemScanExcluded("C:/Users/me/Projects/Windows/notes.txt"));
    }

    void findsStagedDuplicates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath("a.bin"), 64 * 1024);
        writeFile(dir.filePath("b.bin"), 64 * 1024);
        writeFile(dir.filePath("same_size_other.bin"), 64 * 1024 + 0);
        writeFile(dir.filePath("small1.txt"), 512);
        writeFile(dir.filePath("small2.txt"), 512);

        // Make the same-size file genuinely different.
        writeFile(dir.filePath("same_size_other.bin"), 64 * 1024);
        QFile other(dir.filePath("same_size_other.bin"));
        QVERIFY(other.open(QIODevice::ReadWrite));
        other.seek(1024);
        QVERIFY(other.putChar('Z') >= 0);
        other.close();

        QVector<FileEntry> files;
        files << makeFile(dir.filePath("a.bin"), 64 * 1024)
              << makeFile(dir.filePath("b.bin"), 64 * 1024)
              << makeFile(dir.filePath("same_size_other.bin"), 64 * 1024)
              << makeFile(dir.filePath("small1.txt"), 512)
              << makeFile(dir.filePath("small2.txt"), 512);

        DedupService service;
        const DedupResult result = service.findDuplicates(files, 16 * 1024, false);

        QCOMPARE(result.groups.size(), 1);
        QCOMPARE(result.groups.first().files.size(), 2);
        QCOMPARE(result.groups.first().size, qint64(64 * 1024));
        QCOMPARE(result.wastedBytes, qint64(64 * 1024));
        // Only the two identical files reach the full hash stage.
        QCOMPARE(result.hashedFiles, 2);
    }
};

class TestAnalysis : public QObject {
    Q_OBJECT

private slots:
    void junkDetectionCategories()
    {
        const QString root = QStringLiteral("C:/data");
        ScanResult result;
        result.rootPath = root;
        result.files << makeFile(root + "/temp/scratch.tmp", 200)
                     << makeFile(root + "/app.log", 300)
                     << makeFile(root + "/cache/data.cache", 500)
                     << makeFile(root + "/Thumbs.db", 100)
                     << makeFile(root + "/important.bin", 150);

        AnalysisService service{QSqlDatabase()};
        const QVector<JunkGroup> junk = service.junkFiles(result);

        QCOMPARE(junk.size(), 4);
        qint64 total = 0;
        for (const JunkGroup &group : junk) {
            total += group.size;
        }
        QCOMPARE(total, qint64(1100)); // important.bin is not junk
    }

    void forecastUsesSnapshotTrend()
    {
        const QString connection = QStringLiteral("forecast-test");
        QSqlDatabase database = openSchemaDatabase(connection);
        QVERIFY(database.isOpen());

        const QString root = QStringLiteral("C:/forecast");
        const QDateTime base = QDateTime::currentDateTime().addDays(-10);
        for (int index = 0; index < 5; ++index) {
            QSqlQuery query(database);
            query.prepare(QStringLiteral("INSERT INTO snapshots(created_at, root_path, total_size, file_count) VALUES(?, ?, ?, ?)"));
            query.addBindValue(base.addDays(index * 2).toString(Qt::ISODate));
            query.addBindValue(root);
            query.addBindValue(qint64(1000) + index * 100); // +100 bytes every two days
            query.addBindValue(1);
            QVERIFY(query.exec());
        }

        AnalysisService service(database);
        const DiskForecast forecast = service.forecastForRoot(root, nullptr);

        QVERIFY(forecast.available);
        QCOMPARE(forecast.sampleCount, 5);
        // 50 bytes/day expected from the synthetic series.
        QVERIFY(qAbs(forecast.growthBytesPerDay - 50.0) < 1.0);

        {
            QSqlQuery cleanup(database);
            cleanup.exec(QStringLiteral("DELETE FROM snapshots"));
        }
        database.close();
    }
};

class TestReport : public QObject {
    Q_OBJECT

private slots:
    void htmlReportContainsSections()
    {
        ScanResult result;
        result.rootPath = QStringLiteral("C:/report");
        result.files << makeFile(result.rootPath + "/big.iso", 5 * 1024 * 1024)
                     << makeFile(result.rootPath + "/notes.txt", 1024);
        TreeEntry root;
        root.kind = TreeEntryKind::Folder;
        root.path = result.rootPath;
        root.name = QStringLiteral("report");
        root.size = 5 * 1024 * 1024 + 1024;
        result.treeEntries << root;

        TreeEntry child;
        child.kind = TreeEntryKind::Folder;
        child.path = result.rootPath + "/media";
        child.name = QStringLiteral("media");
        child.parentPath = result.rootPath;
        child.size = 5 * 1024 * 1024;
        child.fileCount = 1;
        result.treeEntries << child;

        const QString html = ReportService::buildHtmlReport(result);
        QVERIFY(html.contains(QStringLiteral("Largest files")));
        QVERIFY(html.contains(QStringLiteral("Largest folders")));
        QVERIFY(html.contains(QStringLiteral("File types")));
        QVERIFY(html.contains(QStringLiteral("big.iso")));
        QVERIFY(html.contains(QStringLiteral("C:/report")));
        QVERIFY(html.contains(QStringLiteral("C:/report/media")));
        // Charts: a CSS bar chart for the folders and a stacked bar with legend for types.
        QVERIFY(html.contains(QStringLiteral("<h2>Charts</h2>")));
        QVERIFY(html.contains(QStringLiteral("class='barrow'")));
        QVERIFY(html.contains(QStringLiteral("class='stackseg'")));
        QVERIFY(html.contains(QStringLiteral("class='legend'")));
    }

    void pdfReportIsWrittenWithTables()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("report.pdf"));

        ScanResult result;
        result.rootPath = QStringLiteral("C:/pdfreport");
        result.files << makeFile(result.rootPath + "/videos/movie.mkv", 40 * 1024 * 1024)
                     << makeFile(result.rootPath + "/docs/manual.pdf", 2 * 1024 * 1024)
                     << makeFile(result.rootPath + "/docs/notes.txt", 4096);
        for (int index = 0; index < 12; ++index) {
            TreeEntry folder;
            folder.kind = TreeEntryKind::Folder;
            folder.path = QStringLiteral("%1/folder-%2").arg(result.rootPath).arg(index);
            folder.name = QStringLiteral("folder-%1").arg(index);
            folder.parentPath = result.rootPath;
            folder.size = (12 - index) * 1024 * 1024;
            folder.fileCount = index + 1;
            folder.folderCount = index % 3;
            result.treeEntries << folder;
        }

        QString error;
        QVERIFY2(ReportService::writePdfReport(path, result, {}, &error), qPrintable(error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray payload = file.readAll();
        QVERIFY(payload.size() > 5000);
        QVERIFY(payload.startsWith("%PDF"));
    }

    void csvEscapesQuotes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("out.csv");
        QString error;
        QVERIFY(ReportService::writeCsv(path, {"Name", "Size"},
                                        {{QStringLiteral("a \"quoted\" name"), QStringLiteral("10")}}, &error));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(content.contains(QStringLiteral("\"a \"\"quoted\"\" name\"")));
    }
};

class TestLedger : public QObject {
    Q_OBJECT

private slots:
    void ledgerAndTiers()
    {
        const QString connection = QStringLiteral("ledger-test");
        QSqlDatabase database = openSchemaDatabase(connection);
        QVERIFY(database.isOpen());

        SnapshotService service(database);
        const QString root = QStringLiteral("C:/ledgertest");
        QVERIFY(service.setResolutionRule(root + QStringLiteral("/macro"), ResolutionTier::Macro));
        QVERIFY(service.setResolutionRule(root + QStringLiteral("/blocked"), ResolutionTier::Blacklist));

        auto makeResult = [&](int payload) {
            ScanResult result;
            result.rootPath = root;

            // FolderEntry feeds the ledger, TreeEntry feeds the snapshot change detection.
            auto addFolder = [&](const QString &name, qint64 size, int files) {
                FolderEntry folder;
                folder.path = root + QLatin1Char('/') + name;
                folder.parentPath = root;
                folder.name = name;
                folder.totalSize = size;
                folder.fileCount = files;
                result.folders << folder;

                TreeEntry entry;
                entry.kind = TreeEntryKind::Folder;
                entry.path = folder.path;
                entry.parentPath = folder.parentPath;
                entry.name = folder.name;
                entry.size = size;
                entry.fileCount = files;
                result.treeEntries << entry;
            };
            addFolder(QStringLiteral("normal"), 100 + payload, 1);
            addFolder(QStringLiteral("macro"), 200 + payload, 1);
            addFolder(QStringLiteral("blocked"), 300 + payload, 1);

            TreeEntry rootEntry;
            rootEntry.kind = TreeEntryKind::Folder;
            rootEntry.path = root;
            rootEntry.name = QStringLiteral("ledgertest");
            rootEntry.size = 600 + payload * 3;
            result.treeEntries << rootEntry;

            result.files << makeFile(root + "/normal/file.bin", 100 + payload)
                         << makeFile(root + "/macro/file.bin", 200 + payload)
                         << makeFile(root + "/blocked/file.bin", 300 + payload);
            return result;
        };

        const SnapshotCreateResult first = service.createSnapshot(makeResult(0), 0, nullptr);
        QVERIFY(first.created);
        const int rowsAfterFirst = service.ledgerRowCount(nullptr);
        QCOMPARE(rowsAfterFirst, 3); // root + normal + macro, never blocked

        const SnapshotCreateResult second = service.createSnapshot(makeResult(10), 0, nullptr);
        QVERIFY(second.created);
        QVERIFY(service.ledgerRowCount(nullptr) > rowsAfterFirst);

        // Unchanged snapshot: no new ledger rows.
        const int rowsBeforeNoop = service.ledgerRowCount(nullptr);
        service.createSnapshot(makeResult(10), 0, nullptr);
        QCOMPARE(service.ledgerRowCount(nullptr), rowsBeforeNoop);

        // History carries values forward across every snapshot of the root.
        const QVector<FolderHistoryPoint> history = service.folderHistory(root, root, 20, nullptr);
        QCOMPARE(history.size(), 3);
        QCOMPARE(history.last().size, qint64(100 + 10 + 200 + 10 + 300 + 10));

        // Blocked folder never reaches the ledger.
        QVERIFY(service.folderHistory(root, root + QStringLiteral("/blocked"), 20, nullptr).isEmpty());

        // Macro paths keep folder sizes but no file changelog; blacklisted paths never appear.
        const QVector<SnapshotSummary> summaries = service.listSnapshots(nullptr);
        QVERIFY(!summaries.isEmpty());
        int totalEvents = 0;
        for (const SnapshotSummary &summary : summaries) {
            const QVector<SnapshotFileEvent> events = service.snapshotFileEvents(summary.id, nullptr);
            totalEvents += events.size();
            for (const SnapshotFileEvent &event : events) {
                QVERIFY(!event.path.contains(QStringLiteral("/macro/")));
                QVERIFY(!event.path.contains(QStringLiteral("/blocked/")));
            }
        }
        QVERIFY(totalEvents > 0);

        database.close();
    }
};

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void builtInThemesResolveEveryPlaceholder()
    {
        const QMap<QString, ThemeDefinition> themes = ThemeManager::builtInThemes();
        QCOMPARE(themes.size(), 2);
        QVERIFY(themes.contains(QStringLiteral("dark")));
        QVERIFY(themes.contains(QStringLiteral("light")));

        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            const QString &styleSheet = it.value().styleSheet;
            QVERIFY2(!styleSheet.isEmpty(), qPrintable(it.key()));
            for (int placeholder = 1; placeholder <= 9; ++placeholder) {
                const QString token = QStringLiteral("%%1").arg(placeholder);
                QVERIFY2(!styleSheet.contains(token), qPrintable(QStringLiteral("%1 leaves %2 unresolved").arg(it.key(), token)));
            }
        }
    }

    void buttonsAndInputsCarryTheAccentFrame()
    {
        const QMap<QString, ThemeDefinition> themes = ThemeManager::builtInThemes();
        for (auto it = themes.cbegin(); it != themes.cend(); ++it) {
            const QString &styleSheet = it.value().styleSheet;
            // The accent frame must be global: buttons in the toolbar, panels, dialogs and
            // message boxes all rely on the same unscoped rule.
            QVERIFY(styleSheet.contains(QStringLiteral("QPushButton, QToolButton {")));
            QVERIFY(styleSheet.contains(QStringLiteral("QPushButton:focus, QToolButton:focus")));
            QVERIFY(styleSheet.contains(QStringLiteral("QComboBox:focus, QSpinBox:focus")));
            QVERIFY(styleSheet.contains(QStringLiteral("QPushButton#destructiveButton")));
            QVERIFY(styleSheet.contains(QStringLiteral("QToolButton#destructiveButton")));
            QVERIFY(!styleSheet.contains(QStringLiteral("QDialog QPushButton")));
            QVERIFY(!styleSheet.contains(QStringLiteral("QMessageBox QPushButton")));
        }
    }
};

class TestTableItems : public QObject {
    Q_OBJECT

private slots:
    void numericColumnsSortByValueNotText()
    {
        QTableWidget table;
        table.setColumnCount(2);
        table.setSortingEnabled(false);
        table.setRowCount(3);
        table.setItem(0, 0, makeTextItem(QStringLiteral("big")));
        table.setItem(0, 1, makeNumberItem(QStringLiteral("2.00 MB"), 2 * 1024 * 1024));
        table.setItem(1, 0, makeTextItem(QStringLiteral("small")));
        table.setItem(1, 1, makeNumberItem(QStringLiteral("512 B"), 512));
        table.setItem(2, 0, makeTextItem(QStringLiteral("medium")));
        table.setItem(2, 1, makeNumberItem(QStringLiteral("400 days"), 400));
        table.setSortingEnabled(true);

        // Text sorting would order "2.00 MB" < "400 days" < "512 B"; numeric sorting must
        // give 400 (medium) < 512 (small) < 2 MB (big).
        table.sortItems(1, Qt::AscendingOrder);
        QCOMPARE(table.item(0, 0)->text(), QStringLiteral("medium"));
        QCOMPARE(table.item(1, 0)->text(), QStringLiteral("small"));
        QCOMPARE(table.item(2, 0)->text(), QStringLiteral("big"));

        table.sortItems(1, Qt::DescendingOrder);
        QCOMPARE(table.item(0, 0)->text(), QStringLiteral("big"));
    }

    void percentAndSortGuardBehave()
    {
        QTableWidget table;
        table.setColumnCount(2);
        table.setSortingEnabled(false);
        table.setRowCount(2);
        table.setItem(0, 0, makeTextItem(QStringLiteral("nine")));
        table.setItem(0, 1, makePercentItem(9.5));
        table.setItem(1, 0, makeTextItem(QStringLiteral("forty")));
        table.setItem(1, 1, makePercentItem(42.7));

        // Repopulating under a guard must keep the user's sort selection.
        table.sortItems(1, Qt::DescendingOrder);
        {
            TableSortGuard guard(&table);
            table.setItem(0, 0, makeTextItem(QStringLiteral("replaced")));
        }
        QCOMPARE(table.item(0, 0)->text(), QStringLiteral("replaced"));
        QCOMPARE(table.item(0, 1)->text(), QStringLiteral("42.7%"));
    }
};

class TestEntryActions : public QObject {
    Q_OBJECT

private slots:
    void sharedMenuIsIdenticalEverywhere()
    {
        // Every view builds its context menu through this helper, so the staging entry is
        // guaranteed to appear in the same place with the same wording.
        QMenu menu;
        const SharedEntryActions actions = addSharedEntryActions(menu);
        QVERIFY(actions.showInExplorer != nullptr);
        QVERIFY(actions.copyPath != nullptr);
        QVERIFY(actions.stage != nullptr);

        QStringList labels;
        for (QAction *action : menu.actions()) {
            labels << (action->isSeparator() ? QStringLiteral("-") : action->text());
        }
        QCOMPARE(labels, QStringList({QStringLiteral("Show in Explorer"), QStringLiteral("Copy Path"),
                                      QStringLiteral("-"), QStringLiteral("Stage for Deletion")}));
    }

    void stagingFailsCleanlyWithoutAService()
    {
        EntryActionHub *hub = EntryActionHub::instance();
        hub->setTrashService(nullptr);
        QVERIFY(!hub->isAvailable());

        QString error;
        TreeEntry entry;
        entry.path = QStringLiteral("C:/somewhere/file.bin");
        entry.size = 1024;
        QVERIFY(!hub->stage(entry, &error));
        QVERIFY(!error.isEmpty());
    }

    void everythingMetadataIsAvailable()
    {
        QCOMPARE(EverythingClient::downloadUrl(), QStringLiteral("https://www.voidtools.com/downloads/"));
        // Detection must never throw and must return either an existing file or nothing.
        const QString detected = EverythingClient::detectInstalledExecutable();
        QVERIFY(detected.isEmpty() || QFileInfo::exists(detected));
    }
};

int main(int argc, char *argv[])
{
    // QApplication (not QCoreApplication): the table item suites create real widgets.
    QApplication app(argc, argv);

    // Every suite gets its own report file, so one run keeps all of the results even when
    // the executable is built for the Windows GUI subsystem.
    struct Suite {
        QObject *object;
        const char *name;
    };
    const QVector<Suite> suites = {
        {new TestPathUtils, "pathutils"},
        {new TestSizeFormatter, "sizeformatter"},
        {new TestDedup, "dedup"},
        {new TestAnalysis, "analysis"},
        {new TestReport, "report"},
        {new TestLedger, "ledger"},
        {new TestTheme, "theme"},
        {new TestTableItems, "tableitems"},
        {new TestEntryActions, "entryactions"},
    };

    int status = 0;
    for (const Suite &suite : suites) {
        QVector<QByteArray> arguments;
        arguments.reserve(argc + 2);
        for (int index = 0; index < argc; ++index) {
            arguments.push_back(QByteArray(argv[index]));
        }
        const QByteArray reportPath = QDir(QDir::tempPath())
                                          .filePath(QStringLiteral("opentree-tests-%1.txt")
                                                        .arg(QLatin1String(suite.name)))
                                          .toUtf8();
        arguments.push_back(QByteArrayLiteral("-o"));
        arguments.push_back(reportPath + ",txt");

        QVector<char *> rawArguments;
        rawArguments.reserve(arguments.size());
        for (QByteArray &argument : arguments) {
            rawArguments.push_back(argument.data());
        }

        status |= QTest::qExec(suite.object, rawArguments.size(), rawArguments.data());
        delete suite.object;
    }
    return status;
}

#include "tst_core.moc"
