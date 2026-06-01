#include "processrunner.h"
#include <QRegularExpression>

#ifndef Q_OS_WIN
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
    // Strip trailing shell redirects (2>&1) - QProcess captures both channels natively
    QString cleaned = cmd.trimmed();
    cleaned.remove(QRegularExpression(R"(\s*2>&1\s*$)"));

#ifdef Q_OS_WIN
    // On Windows, run the executable directly (no cmd.exe wrapper).
    // Use setNativeArguments so Qt does not re-escape quotes.
    // Extract the program (first token, possibly quoted) and pass rest as native args.
    QString program;
    QString args;
    if (cleaned.startsWith('"')) {
        int end = cleaned.indexOf('"', 1);
        if (end > 0) {
            program = cleaned.mid(1, end - 1);
            args = cleaned.mid(end + 1).trimmed();
        } else {
            program = cleaned;
        }
    } else {
        int space = cleaned.indexOf(' ');
        if (space > 0) {
            program = cleaned.left(space);
            args = cleaned.mid(space + 1).trimmed();
        } else {
            program = cleaned;
        }
    }
    proc->setProgram(program);
    proc->setNativeArguments(args);
#else
    proc->setProgram("sh");
    proc->setArguments(QStringList() << "-c" << cleaned);
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
            proc->disconnect();
#ifndef Q_OS_WIN
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
