#include "Database.h"

#include <QDir>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

QString nowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QDateTime fromDb(const QVariant &v)
{
    return QDateTime::fromString(v.toString(), Qt::ISODateWithMs).toLocalTime();
}

QString toJson(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject fromJson(const QVariant &v)
{
    return QJsonDocument::fromJson(v.toString().toUtf8()).object();
}

} // namespace

Database::Database() : m_connection(QStringLiteral("aqualens-history")) {}

Database::~Database()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool Database::exec(const QString &sql, QString *error)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

bool Database::open(QString *error)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_path = QDir(dir).filePath(QStringLiteral("history.db"));

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    const bool ok =
        exec(QStringLiteral(
                 "CREATE TABLE IF NOT EXISTS sessions ("
                 " id            INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " title         TEXT    NOT NULL,"
                 " file_name     TEXT    NOT NULL,"
                 " created_at    TEXT    NOT NULL,"
                 " updated_at    TEXT    NOT NULL,"
                 " pinned        INTEGER NOT NULL DEFAULT 0,"
                 " row_count     INTEGER NOT NULL DEFAULT 0,"
                 " param_count   INTEGER NOT NULL DEFAULT 0,"
                 " anomaly_count INTEGER NOT NULL DEFAULT 0,"
                 " settings      TEXT,"
                 " summary       TEXT)"),
             error)
        && exec(QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS session_data ("
                    " session_id INTEGER PRIMARY KEY REFERENCES sessions(id) ON DELETE CASCADE,"
                    " csv        BLOB NOT NULL)"),
                error)
        && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sessions_updated ON sessions(updated_at)"), error)
        && exec(QStringLiteral("PRAGMA user_version = 1"), error);
    return ok;
}

qint64 Database::createSession(const QString &title, const QString &fileName, const QByteArray &csv,
                               const QJsonObject &settings, const QJsonObject &summary,
                               int rows, int params, int anomalies)
{
    if (!m_db.transaction())
        return -1;

    const QString now = nowUtc();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions (title, file_name, created_at, updated_at, row_count, param_count,"
        " anomaly_count, settings, summary) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(title);
    q.addBindValue(fileName);
    q.addBindValue(now);
    q.addBindValue(now);
    q.addBindValue(rows);
    q.addBindValue(params);
    q.addBindValue(anomalies);
    q.addBindValue(toJson(settings));
    q.addBindValue(toJson(summary));
    if (!q.exec()) {
        m_db.rollback();
        return -1;
    }
    const qint64 id = q.lastInsertId().toLongLong();

    QSqlQuery d(m_db);
    d.prepare(QStringLiteral("INSERT INTO session_data (session_id, csv) VALUES (?, ?)"));
    d.addBindValue(id);
    d.addBindValue(qCompress(csv, 6));
    if (!d.exec()) {
        m_db.rollback();
        return -1;
    }
    return m_db.commit() ? id : -1;
}

bool Database::updateAnalysis(qint64 id, const QJsonObject &settings, const QJsonObject &summary, int anomalies)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE sessions SET settings = ?, summary = ?, anomaly_count = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(toJson(settings));
    q.addBindValue(toJson(summary));
    q.addBindValue(anomalies);
    q.addBindValue(nowUtc());
    q.addBindValue(id);
    return q.exec();
}

bool Database::rename(qint64 id, const QString &title)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE sessions SET title = ? WHERE id = ?"));
    q.addBindValue(title);
    q.addBindValue(id);
    return q.exec();
}

bool Database::setPinned(qint64 id, bool pinned)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE sessions SET pinned = ? WHERE id = ?"));
    q.addBindValue(pinned ? 1 : 0);
    q.addBindValue(id);
    return q.exec();
}

bool Database::remove(qint64 id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}

QVector<SessionInfo> Database::listSessions() const
{
    QVector<SessionInfo> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, title, file_name, created_at, updated_at, pinned, row_count, param_count,"
            " anomaly_count FROM sessions ORDER BY pinned DESC, updated_at DESC")))
        return out;
    while (q.next()) {
        SessionInfo s;
        s.id = q.value(0).toLongLong();
        s.title = q.value(1).toString();
        s.fileName = q.value(2).toString();
        s.created = fromDb(q.value(3));
        s.updated = fromDb(q.value(4));
        s.pinned = q.value(5).toInt() != 0;
        s.rows = q.value(6).toInt();
        s.params = q.value(7).toInt();
        s.anomalies = q.value(8).toInt();
        out << s;
    }
    return out;
}

bool Database::loadSession(qint64 id, SessionRecord &out) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT s.id, s.title, s.file_name, s.created_at, s.updated_at, s.pinned, s.row_count,"
        " s.param_count, s.anomaly_count, s.settings, s.summary, d.csv"
        " FROM sessions s JOIN session_data d ON d.session_id = s.id WHERE s.id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    out.info.id = q.value(0).toLongLong();
    out.info.title = q.value(1).toString();
    out.info.fileName = q.value(2).toString();
    out.info.created = fromDb(q.value(3));
    out.info.updated = fromDb(q.value(4));
    out.info.pinned = q.value(5).toInt() != 0;
    out.info.rows = q.value(6).toInt();
    out.info.params = q.value(7).toInt();
    out.info.anomalies = q.value(8).toInt();
    out.settings = fromJson(q.value(9));
    out.summary = fromJson(q.value(10));
    out.csv = qUncompress(q.value(11).toByteArray());
    return !out.csv.isEmpty();
}
