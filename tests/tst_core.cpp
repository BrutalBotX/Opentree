// Core logic tests. These cover the non-UI pieces: path helpers, size formatting, duplicate
// detection, analysis, report generation and the snapshot ledger with its three-tier routing.
#include <QtTest>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "domain/ScanTypes.h"
#include "services/AnalysisService.h"
#include "services/DedupService.h"
#include "services/ReportService.h"
#include "services/SnapshotService.h"
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

        const QString html = ReportService::buildHtmlReport(result);
        QVERIFY(html.contains(QStringLiteral("Largest files")));
        QVERIFY(html.contains(QStringLiteral("File types")));
        QVERIFY(html.contains(QStringLiteral("big.iso")));
        QVERIFY(html.contains(QStringLiteral("C:/report")));
        QVERIFY(!html.contains(QStringLiteral("Largest folders")));
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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

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
