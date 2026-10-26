#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

/** One row of the history sidebar (no CSV payload). */
struct SessionInfo
{
    qint64 id = -1;
    QString title;
    QString fileName;
    QDateTime created;
    QDateTime updated;
    bool pinned = false;
    int rows = 0;
    int params = 0;
    int anomalies = 0;
};

/** Everything needed to restore an analysis. */
struct SessionRecord
{
    SessionInfo info;
    QByteArray csv;
    QJsonObject settings;
    QJsonObject summary;
};

/**
 * SQLite-backed analysis history ("chats").
 *
 *  sessions(id, title, file_name, created_at, updated_at, pinned,
 *           row_count, param_count, anomaly_count, settings, summary)
 *  session_data(session_id -> sessions.id ON DELETE CASCADE, csv BLOB [zlib])
 *
 * The raw CSV is stored with every session, so history keeps working
 * even when the original file is moved or deleted.
 */
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(QString *error = nullptr);
    QString path() const { return m_path; }

    qint64 createSession(const QString &title, const QString &fileName, const QByteArray &csv,
                         const QJsonObject &settings, const QJsonObject &summary,
                         int rows, int params, int anomalies);
    bool updateAnalysis(qint64 id, const QJsonObject &settings, const QJsonObject &summary, int anomalies);
    bool rename(qint64 id, const QString &title);
    bool setPinned(qint64 id, bool pinned);
    bool remove(qint64 id);

    QVector<SessionInfo> listSessions() const;
    bool loadSession(qint64 id, SessionRecord &out) const;

private:
    bool exec(const QString &sql, QString *error = nullptr);

    QString m_connection;
    QString m_path;
    QSqlDatabase m_db;
};
