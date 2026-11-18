// Core logic tests. These cover the non-UI pieces: path helpers, size formatting, duplicate
// detection, analysis, report generation and the snapshot ledger with its three-tier routing.
#include <QtTest>
#include <QCompleter>
#include <QDir>
#include <QLineEdit>
#include <QMenu>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringListModel>
#include <QTemporaryDir>

#include "domain/ScanTypes.h"
#include "services/AnalysisService.h"
#include "services/DatabaseMaintenanceService.h"
#include "services/DedupService.h"
#include "services/ReportService.h"
#include "services/SnapshotService.h"
#include "services/SweepService.h"
#include "services/UpdateChecker.h"
#include "services/VirtualTrashService.h"
#include "integrations/EverythingClient.h"
#include "ui/AddressBarAutocomplete.h"
#include "ui/EntryActions.h"
#include "ui/TableItems.h"
#include "ui/ThemeManager.h"
#include "utils/PathUtils.h"
#include "utils/SizeFormatter.h"

using namespace opentree;

namespace {

// A scratch directory for tests that must not hit Windows "system path" filters
// (QTemporaryDir lives under AppData/Local/Temp, which the duplicate scanner excludes).
class ScopedTestDir {
public:
    ScopedTestDir()
    {
        const QString base = QDir::current().absoluteFilePath(QStringLiteral("opentree-test-scratch"));
        m_path = base + QStringLiteral("-") + QString::number(QCoreApplication::applicationPid());
        QDir().mkpath(m_path);
    }

    ~ScopedTestDir()
    {
        QDir(m_path).removeRecursively();
    }

    QString path() const { return m_path; }
    QString filePath(const QString &name) const { return m_path + QLatin1Char('/') + name; }
    bool isValid() const { return QDir(m_path).exists(); }

private:
    QString m_path;
};

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

// A file-backed schema database, needed by the maintenance service because VACUUM and the
// on-disk size accounting operate on a real file rather than an in-memory database.
QSqlDatabase openFileSchemaDatabase(const QString &connectionName, const QString &path)
{
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    database.setDatabaseName(path);
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
                folder.name = name;
                folder.totalSize = size;
                folder.fileCount = files;
                result.folders << folder;

                TreeEntry entry;
                entry.kind = TreeEntryKind::Folder;
                entry.path = folder.path;
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

    void compareRowsAndSummary()
    {
        QSqlDatabase database = openSchemaDatabase(QStringLiteral("compare-test"));
        QVERIFY(database.isOpen());

        SnapshotService service(database);
        const QString root = QStringLiteral("C:/comparetree");

        auto makeResult = [&](qint64 normalSize, bool includeAdded, qint64 addedSize) {
            ScanResult result;
            result.rootPath = root;

            auto addFolder = [&](const QString &name, qint64 size) {
                FolderEntry folder;
                folder.path = root + QLatin1Char('/') + name;
                folder.name = name;
                folder.totalSize = size;
                folder.fileCount = 1;
                result.folders << folder;

                TreeEntry entry;
                entry.kind = TreeEntryKind::Folder;
                entry.path = folder.path;
                entry.name = name;
                entry.size = size;
                entry.fileCount = 1;
                result.treeEntries << entry;
            };
            addFolder(QStringLiteral("normal"), normalSize);
            qint64 rootSize = normalSize;
            if (includeAdded) {
                addFolder(QStringLiteral("added"), addedSize);
                rootSize += addedSize;
            }

            TreeEntry rootEntry;
            rootEntry.kind = TreeEntryKind::Folder;
            rootEntry.path = root;
            rootEntry.name = QStringLiteral("comparetree");
            rootEntry.size = rootSize;
            result.treeEntries << rootEntry;

            result.files << makeFile(root + QStringLiteral("/normal/file.bin"), normalSize);
            if (includeAdded) {
                result.files << makeFile(root + QStringLiteral("/added/file.bin"), addedSize);
            }
            return result;
        };

        QVERIFY(service.createSnapshot(makeResult(100, false, 0), 0, nullptr).created);
        const QVector<SnapshotSummary> summaries = service.listSnapshots(nullptr);
        QVERIFY(!summaries.isEmpty());
        const int snapshotId = summaries.first().id;

        // Unchanged tree: there is nothing to compare.
        const SnapshotCompareResult unchanged =
            service.compareSnapshotToCurrent(snapshotId, makeResult(100, false, 0), nullptr);
        QVERIFY(!unchanged.found);

        // The folder grew from 100 to 250 and a second one appeared with 400.
        const ScanResult current = makeResult(250, true, 400);
        const SnapshotCompareResult summary = service.compareSnapshotToCurrent(snapshotId, current, nullptr);
        QVERIFY(summary.found);
        QCOMPARE(summary.changedFolderCount, 3);        // normal, added and the root
        QCOMPARE(summary.totalDeltaBytes, qint64(550)); // the root delta only: children are inside it
        QCOMPARE(summary.largestGrowthPath, root);
        QCOMPARE(summary.largestGrowthBytes, qint64(550));
        QVERIFY(!summary.snapshotCreatedAt.isEmpty());

        const QVector<SnapshotCompareRow> rows = service.compareSnapshotRows(snapshotId, current, nullptr);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.first().path, root); // biggest absolute delta first
        bool sawAdded = false;
        bool sawNormal = false;
        for (const SnapshotCompareRow &row : rows) {
            QVERIFY(row.deltaBytes != 0);
            if (row.path == root + QStringLiteral("/added")) {
                sawAdded = true;
                QCOMPARE(row.previousSize, qint64(0));
                QCOMPARE(row.currentSize, qint64(400));
                QCOMPARE(row.percentChangeText, QStringLiteral("new"));
            } else if (row.path == root + QStringLiteral("/normal")) {
                sawNormal = true;
                QCOMPARE(row.previousSize, qint64(100));
                QCOMPARE(row.currentSize, qint64(250));
                QCOMPARE(row.deltaBytes, qint64(150));
                QCOMPARE(row.percentChangeText, QStringLiteral("150.0%"));
            }
        }
        QVERIFY(sawAdded);
        QVERIFY(sawNormal);

        database.close();
    }

    void retentionPrunesOldFileEvents()
    {
        QSqlDatabase database = openSchemaDatabase(QStringLiteral("retention-test"));
        QVERIFY(database.isOpen());

        SnapshotService service(database);
        const QString root = QStringLiteral("C:/retentiontree");

        ScanResult result;
        result.rootPath = root;
        FolderEntry folder;
        folder.path = root;
        folder.name = QStringLiteral("retentiontree");
        folder.totalSize = 10;
        folder.fileCount = 1;
        result.folders << folder;
        TreeEntry entry;
        entry.kind = TreeEntryKind::Folder;
        entry.path = root;
        entry.name = QStringLiteral("retentiontree");
        entry.size = 10;
        entry.fileCount = 1;
        result.treeEntries << entry;
        result.files << makeFile(root + QStringLiteral("/a.bin"), 10);

        QVERIFY(service.createSnapshot(result, 0, nullptr).created);

        // Back-date the snapshot beyond the retention window and prune: its events go, the
        // snapshot itself stays.
        QSqlQuery backdate(database);
        backdate.prepare(QStringLiteral("UPDATE snapshots SET created_at = ?"));
        backdate.addBindValue(QDateTime::currentDateTime().addDays(-30).toString(Qt::ISODate));
        QVERIFY(backdate.exec());

        QCOMPARE(service.compactFileEvents(7, nullptr), 1);
        QCOMPARE(service.compactFileEvents(7, nullptr), 0); // nothing left to prune
        QCOMPARE(service.listSnapshots(nullptr).size(), 1); // pruning keeps the snapshot

        // Snapshot pruning removes the whole old snapshot (items, events, ledger rows) and
        // leaves newer ones alone.
        ScanResult recent = result;
        recent.files.clear();
        recent.files << makeFile(root + QStringLiteral("/b.bin"), 20);
        QVERIFY(service.createSnapshot(recent, 0, nullptr).created);
        QCOMPARE(service.listSnapshots(nullptr).size(), 2);

        // "Keep forever" prunes nothing.
        QCOMPARE(service.pruneOldSnapshots(0, nullptr), 0);
        QCOMPARE(service.listSnapshots(nullptr).size(), 2);

        QCOMPARE(service.pruneOldSnapshots(7, nullptr), 1);
        const QVector<SnapshotSummary> remaining = service.listSnapshots(nullptr);
        QCOMPARE(remaining.size(), 1);
        QVERIFY(remaining.first().createdAt >= QDateTime::currentDateTime().addDays(-7).toString(Qt::ISODate));

        QSqlQuery orphanCheck(database);
        QVERIFY(orphanCheck.exec(QStringLiteral("SELECT COUNT(*) FROM snapshot_items WHERE snapshot_id NOT IN (SELECT id FROM snapshots)")));
        QVERIFY(orphanCheck.next());
        QCOMPARE(orphanCheck.value(0).toInt(), 0);
        QSqlQuery ledgerCheck(database);
        QVERIFY(ledgerCheck.exec(QStringLiteral("SELECT COUNT(*) FROM snapshot_ledger WHERE snapshot_id NOT IN (SELECT id FROM snapshots)")));
        QVERIFY(ledgerCheck.next());
        QCOMPARE(ledgerCheck.value(0).toInt(), 0);

        database.close();
    }
};

class TestDatabaseMaintenance : public QObject {
    Q_OBJECT

private slots:
    void statsPruneAndVacuum()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString dbPath = dir.filePath(QStringLiteral("maintenance.db"));

        {
            QSqlDatabase database = openFileSchemaDatabase(QStringLiteral("maintenance-test"), dbPath);
            QVERIFY(database.isOpen());
            DatabaseMaintenanceService service(database, dbPath);

            // One old snapshot (past the retention window) and one recent snapshot.
            QSqlQuery oldSnapshot(database);
            oldSnapshot.prepare(QStringLiteral("INSERT INTO snapshots(created_at, root_path, total_size, file_count) VALUES(?, ?, ?, ?)"));
            oldSnapshot.addBindValue(QDateTime::currentDateTime().addDays(-30).toString(Qt::ISODate));
            oldSnapshot.addBindValue(QStringLiteral("C:/old"));
            oldSnapshot.addBindValue(qint64(1000));
            oldSnapshot.addBindValue(1);
            QVERIFY(oldSnapshot.exec());

            QSqlQuery recentSnapshot(database);
            recentSnapshot.prepare(QStringLiteral("INSERT INTO snapshots(created_at, root_path, total_size, file_count) VALUES(?, ?, ?, ?)"));
            recentSnapshot.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
            recentSnapshot.addBindValue(QStringLiteral("C:/recent"));
            recentSnapshot.addBindValue(qint64(2000));
            recentSnapshot.addBindValue(1);
            QVERIFY(recentSnapshot.exec());

            QSqlQuery events(database);
            events.prepare(QStringLiteral(
                "INSERT INTO snapshot_file_events(snapshot_id, event_type, path, parent_path, name, old_size, new_size) "
                "VALUES(1, 'MODIFY', 'C:/old/a.bin', 'C:/old', 'a.bin', 0, 10),"
                       "(2, 'ADD', 'C:/recent/b.bin', 'C:/recent', 'b.bin', 0, 20)"));
            QVERIFY(events.exec());

            // Stats reflect the schema tables and a non-zero on-disk size.
            DatabaseStats stats = service.collectStats(nullptr);
            QVERIFY(stats.fileSizeBytes > 0);
            QVERIFY(stats.tables.size() >= 10);
            auto rowCount = [&stats](const QString &name) -> qint64 {
                for (const DatabaseTableStats &table : stats.tables) {
                    if (table.name == name) {
                        return table.rowCount;
                    }
                }
                return -1;
            };
            QCOMPARE(rowCount(QStringLiteral("snapshots")), qint64(2));
            QCOMPARE(rowCount(QStringLiteral("snapshot_file_events")), qint64(2));

            // Pruning drops only the events belonging to snapshots past the window.
            QCOMPARE(service.pruneFileEvents(7, nullptr), 1);
            {
                QSqlQuery remaining(database);
                QVERIFY(remaining.exec(QStringLiteral("SELECT COUNT(*) FROM snapshot_file_events")));
                QVERIFY(remaining.next());
                QCOMPARE(remaining.value(0).toInt(), 1);
            }

            // VACUUM succeeds on the file-backed database.
            QString vacuumError;
            const DatabaseMaintenanceResult vacuum = service.vacuum(&vacuumError);
            QVERIFY2(vacuum.ok, qPrintable(QStringLiteral("VACUUM failed: %1").arg(vacuumError)));

            database.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("maintenance-test"));
    }
};

class TestAddressBarAutocomplete : public QObject {
    Q_OBJECT

private slots:
    void installsAndRefreshesTheCompleter()
    {
        QLineEdit addressBar;
        QVERIFY(addressBar.completer() == nullptr);

        AddressBarAutocomplete::apply(&addressBar, {QStringLiteral("C:/alpha"), QStringLiteral("C:/beta")});
        QCompleter *completer = addressBar.completer();
        QVERIFY(completer != nullptr);
        QCOMPARE(addressBar.findChild<QCompleter *>(QStringLiteral("addressCompleter")), completer);
        QCOMPARE(completer->model()->rowCount(), 2);

        // Applying again replaces the model instead of stacking completers.
        AddressBarAutocomplete::apply(&addressBar, {QStringLiteral("C:/gamma")});
        QCOMPARE(addressBar.completer(), completer);
        QCOMPARE(completer->model()->rowCount(), 1);

        // Empty entries are dropped and duplicates (case-insensitive) collapse.
        AddressBarAutocomplete::apply(&addressBar, {QString(), QStringLiteral("C:/beta"),
                                                    QStringLiteral("c:/BETA"), QStringLiteral("C:/delta")});
        QCOMPARE(completer->model()->rowCount(), 2);
    }

    void capsTheSuggestionList()
    {
        QLineEdit addressBar;
        QStringList many;
        many.reserve(AddressBarAutocomplete::kMaxSuggestions + 50);
        for (int index = 0; index < AddressBarAutocomplete::kMaxSuggestions + 50; ++index) {
            many << QStringLiteral("C:/folder-%1").arg(index);
        }

        AddressBarAutocomplete::apply(&addressBar, many);
        QCOMPARE(addressBar.completer()->model()->rowCount(), AddressBarAutocomplete::kMaxSuggestions);
    }

    void nullBarIsIgnored()
    {
        AddressBarAutocomplete::apply(nullptr, {QStringLiteral("C:/anything")});
    }
};

class TestSweep : public QObject {
    Q_OBJECT

private slots:
    void stagesDuplicatesAndJunk()
    {
        QSqlDatabase database = openSchemaDatabase(QStringLiteral("sweep-test"));
        QVERIFY(database.isOpen());

        ScopedTestDir dir;
        QVERIFY(dir.isValid());
        // Two identical 64 KB files (over the 16 KB duplicate threshold), one unique file,
        // and one junk-named file that should be staged as a junk candidate. Names avoid
        // the junk patterns ("temp", "log", ...) so only the intended file is junk.
        writeFile(dir.filePath("alpha.bin"), 64 * 1024);
        writeFile(dir.filePath("bravo.bin"), 64 * 1024);
        writeFile(dir.filePath("unique.dat"), 4096);
        writeFile(dir.filePath("reports.cache"), 32 * 1024);

        ScanResult result;
        result.rootPath = dir.path();
        result.files << makeFile(dir.filePath("alpha.bin"), 64 * 1024)
                     << makeFile(dir.filePath("bravo.bin"), 64 * 1024)
                     << makeFile(dir.filePath("unique.dat"), 4096)
                     << makeFile(dir.filePath("reports.cache"), 32 * 1024);

        SweepService sweep(database);
        const SweepResult staged = sweep.run(result, true, true, 16 * 1024, 0);

        QCOMPARE(staged.duplicateCandidates, 1);
        QCOMPARE(staged.junkCandidates, 1);
        QCOMPARE(staged.stagedCount, 2);          // one duplicate copy + one junk file
        QVERIFY(staged.stagedBytes > 0);

        VirtualTrashService trash(database);
        const QVector<TrashItem> items = trash.stagedItems(nullptr);
        QCOMPARE(items.size(), 2);
        QStringList paths;
        for (const TrashItem &item : items) {
            paths << item.path;
            QVERIFY(item.reason.startsWith(QStringLiteral("Scheduled sweep")));
        }
        QVERIFY(paths.contains(dir.filePath("reports.cache")));
        // The duplicate pair must contribute exactly one copy, never both.
        const bool stagedA = paths.contains(dir.filePath("alpha.bin"));
        const bool stagedB = paths.contains(dir.filePath("bravo.bin"));
        QVERIFY2(stagedA != stagedB, "exactly one duplicate copy is staged");

        // A second run stages nothing new (same paths upsert) and stays within the cap.
        const SweepResult again = sweep.run(result, true, true, 16 * 1024, 0);
        QCOMPARE(trash.stagedItems(nullptr).size(), 2);
        QVERIFY(again.stagedCount <= 2);

        database.close();
    }

    void honorsTheJunkMinimumSize()
    {
        QSqlDatabase database = openSchemaDatabase(QStringLiteral("sweep-junkmin-test"));
        QVERIFY(database.isOpen());

        ScopedTestDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath("small.cache"), 1024);
        writeFile(dir.filePath("large.cache"), 512 * 1024);

        ScanResult result;
        result.rootPath = dir.path();
        result.files << makeFile(dir.filePath("small.cache"), 1024)
                     << makeFile(dir.filePath("large.cache"), 512 * 1024);

        SweepService sweep(database);
        const SweepResult staged = sweep.run(result, false, true, 0, 256 * 1024);
        QCOMPARE(staged.stagedCount, 1);

        VirtualTrashService trash(database);
        const QVector<TrashItem> items = trash.stagedItems(nullptr);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().path, dir.filePath("large.cache"));

        database.close();
    }
};

class TestTheme : public QObject {
    Q_OBJECT

private slots:
    void builtInThemesResolveEveryPlaceholder()
    {
        const QMap<QString, ThemeDefinition> themes = ThemeManager::builtInThemes();
        QCOMPARE(themes.size(), 4);
        QVERIFY(themes.contains(QStringLiteral("dark")));
        QVERIFY(themes.contains(QStringLiteral("light")));
        QVERIFY(themes.contains(QStringLiteral("blueprint")));
        QVERIFY(themes.contains(QStringLiteral("planets")));

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
            // Structural borders must use the neutral border token, not the selection colour:
            // check the resolved grid-line colour is low-saturation and differs from the
            // highlight.
            const QRegularExpression gridlinePattern(QStringLiteral("gridline-color: (#[0-9a-fA-F]{6});"));
            const QRegularExpressionMatch gridlineMatch = gridlinePattern.match(styleSheet);
            QVERIFY2(gridlineMatch.hasMatch(), qPrintable(it.key()));
            const QColor gridline(gridlineMatch.captured(1));
            QVERIFY(gridline.isValid());
            // Only the neutral themes must keep structure grey; Blueprint and Planets are
            // deliberately tinted.
            const bool neutralTheme = it.key() == QStringLiteral("dark") || it.key() == QStringLiteral("light");
            if (neutralTheme) {
                QVERIFY2(gridline.hsvSaturationF() < 0.25f,
                         qPrintable(QStringLiteral("%1 grid lines are not neutral: %2").arg(it.key(), gridline.name())));
            }
            QVERIFY2(gridline != it.value().palette.color(QPalette::Highlight),
                     qPrintable(QStringLiteral("%1 still draws structure with the selection colour").arg(it.key())));

            // The graph node style travels with the theme.
            QCOMPARE(it.value().graphStyle,
                     it.key() == QStringLiteral("planets") ? QStringLiteral("planets") : QStringLiteral("neutral"));
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

class TestUpdateChecker : public QObject {
    Q_OBJECT

private slots:
    void comparesVersions_data()
    {
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("newer");

        QTest::newRow("same") << "0.12.1" << "0.12.1" << false;
        QTest::newRow("patch bump") << "0.12.2" << "0.12.1" << true;
        QTest::newRow("minor bump") << "0.13.0" << "0.12.9" << true;
        QTest::newRow("major bump") << "1.0.0" << "0.99.9" << true;
        QTest::newRow("double digit minor") << "0.10.0" << "0.9.0" << true;
        QTest::newRow("older") << "0.11.9" << "0.12.0" << false;
        QTest::newRow("tag prefix") << "v0.13.0" << "0.12.1" << true;
        QTest::newRow("suffix ignored") << "0.13.0-beta" << "0.12.1" << true;
        QTest::newRow("shorter current means newer") << "0.12.1" << "0.12" << true;
        QTest::newRow("shorter candidate is older") << "0.12" << "0.12.1" << false;
        QTest::newRow("alpha tag is not newer") << "v0.1.0-alpha" << "0.12.1" << false;
    }

    void comparesVersions()
    {
        QFETCH(QString, candidate);
        QFETCH(QString, current);
        QFETCH(bool, newer);
        QCOMPARE(UpdateChecker::isNewerVersion(candidate, current), newer);
    }

    void exposesTheRepositoryPages()
    {
        QVERIFY(UpdateChecker::repositoryUrl().startsWith(QStringLiteral("https://github.com/")));
        QVERIFY(UpdateChecker::releasesPageUrl().endsWith(QStringLiteral("/releases")));
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
        {new TestDatabaseMaintenance, "databasemaintenance"},
        {new TestAddressBarAutocomplete, "addressbarautocomplete"},
        {new TestSweep, "sweep"},
        {new TestTheme, "theme"},
        {new TestTableItems, "tableitems"},
        {new TestEntryActions, "entryactions"},
        {new TestUpdateChecker, "updatechecker"},
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
