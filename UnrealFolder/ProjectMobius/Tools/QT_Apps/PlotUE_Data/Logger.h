#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>

/**
 * @brief Lightweight, thread-safe file logger for the Qt helpers.
 *
 * Logger writes to the OS temp directory (PlotUE_Data.log) and mirrors output
 * to qDebug for convenience during development. Access via the static
 * @ref instance() singleton and helper macros below.
 */
class Logger : public QObject
{
    Q_OBJECT
public:
    static Logger& instance()
    {
        static Logger inst;
        return inst;
    }

    /**
     * @brief Write a single formatted line to the log file and console.
     * @param level Short severity label (e.g., INFO, WARN).
     * @param message Message text; newlines are not required.
     */
    void log(const QString& level, const QString& message)
    {
        QMutexLocker locker(&m_mutex);
        if (!m_file.isOpen()) return;

        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
        QString line = QString("[%1] [%2] %3\n").arg(timestamp, level, message);

        m_stream << line;
        m_stream.flush();

        // Also print to console if available
        qDebug().noquote() << line.trimmed();
    }

    void logDebug(const QString& msg) { log("DEBUG", msg); }
    void logInfo(const QString& msg) { log("INFO", msg); }
    void logWarning(const QString& msg) { log("WARN", msg); }
    void logError(const QString& msg) { log("ERROR", msg); }

private:
    Logger()
    {
        QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + "/PlotUE_Data.log";
        m_file.setFileName(logPath);
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append))
        {
            m_stream.setDevice(&m_file);
            logInfo(QString("=== Log started at %1 ===").arg(logPath));
        }
    }

    ~Logger()
    {
        if (m_file.isOpen())
        {
            logInfo("=== Log ended ===");
            m_file.close();
        }
    }

    QFile m_file;
    QTextStream m_stream;
    QMutex m_mutex;
};

// Convenience macros
#define LOG_DEBUG(msg) Logger::instance().logDebug(msg)
#define LOG_INFO(msg) Logger::instance().logInfo(msg)
#define LOG_WARN(msg) Logger::instance().logWarning(msg)
#define LOG_ERROR(msg) Logger::instance().logError(msg)

#endif // LOGGER_H
