#include "processrunner.h"
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

ProcessRunner::ProcessRunner(QObject *parent) : QObject(parent) {}

QStringList ProcessRunner::splitPipe(const QString &command)
{
    // Split on pipe characters that are not inside quotes
    QStringList parts;
    QString current;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (int i = 0; i < command.length(); ++i) {
        QChar c = command[i];
        if (c == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
            current += c;
        } else if (c == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
            current += c;
        } else if (c == '|' && !inSingleQuote && !inDoubleQuote) {
            parts << current.trimmed();
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.trimmed().isEmpty()) {
        parts << current.trimmed();
    }
    return parts;
}

void ProcessRunner::startProcess(QProcess *proc, const QString &cmd)
{
#ifdef Q_OS_WIN
    // On Windows, use cmd.exe with /c. The entire command must be wrapped
    // in an extra pair of quotes so cmd.exe correctly handles inner quoted paths.
    // We use setNativeArguments to prevent Qt from re-escaping the quotes.
    proc->setProgram("cmd.exe");
    proc->setNativeArguments("/c \"" + cmd + "\"");
#else
    proc->setProgram("sh");
    proc->setArguments(QStringList() << "-c" << cmd);
    // Create new process group so we can kill all children
    proc->setChildProcessModifier([]() {
        ::setsid();
    });
#endif
}

void ProcessRunner::run(const QString &command)
{
    cleanup();

    QStringList pipeSegments = splitPipe(command);

    if (pipeSegments.size() == 1) {
        // Simple command, no pipe
        auto *proc = new QProcess(this);
        m_processes << proc;

        connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
            emit outputReady(proc->readAllStandardError());
        });
        connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
            emit outputReady(proc->readAllStandardOutput());
        });
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
            emit finished(exitCode);
        });

        startProcess(proc, pipeSegments[0]);
        proc->start();
        return;
    }

    // Multi-stage pipe: connect stdout of each process to stdin of the next
    for (int i = 0; i < pipeSegments.size(); ++i) {
        auto *proc = new QProcess(this);
        m_processes << proc;

        // Connect stderr of all processes to our output
        connect(proc, &QProcess::readyReadStandardError, this, [this, proc]() {
            emit outputReady(proc->readAllStandardError());
        });

        startProcess(proc, pipeSegments[i]);
    }

    // Chain: pipe stdout of process N to stdin of process N+1
    for (int i = 0; i < m_processes.size() - 1; ++i) {
        QProcess *sender = m_processes[i];
        QProcess *receiver = m_processes[i + 1];

        connect(sender, &QProcess::readyReadStandardOutput, this, [sender, receiver]() {
            QByteArray data = sender->readAllStandardOutput();
            if (receiver->state() == QProcess::Running) {
                receiver->write(data);
            }
        });

        connect(sender, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [receiver](int, QProcess::ExitStatus) {
            receiver->closeWriteChannel();
        });
    }

    // Last process signals completion
    QProcess *last = m_processes.last();
    connect(last, &QProcess::readyReadStandardOutput, this, [this, last]() {
        emit outputReady(last->readAllStandardOutput());
    });
    connect(last, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus) {
        emit finished(exitCode);
    });

    // Start all processes (first to last)
    // Start the receivers first so they're ready, then start the senders
    for (int i = m_processes.size() - 1; i >= 0; --i) {
        m_processes[i]->start();
    }
}

void ProcessRunner::abort()
{
    foreach (QProcess *proc, m_processes) {
        if (proc->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
            // On Windows, kill the entire process tree (cmd.exe + child ffmpeg etc.)
            qint64 pid = proc->processId();
            if (pid > 0) {
                QProcess::startDetached("taskkill", QStringList() << "/T" << "/F" << "/PID" << QString::number(pid));
            }
#else
            // On Unix, kill the process group
            ::kill(-proc->processId(), SIGTERM);
#endif
            proc->kill();
            proc->waitForFinished(3000);
        }
    }
    cleanup();
}

void ProcessRunner::cleanup()
{
    foreach (QProcess *proc, m_processes) {
        proc->deleteLater();
    }
    m_processes.clear();
}
