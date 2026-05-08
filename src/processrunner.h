#pragma once

/**
 * @file processrunner.h
 * @brief Cross-platform process execution with pipe chain support.
 *
 * ProcessRunner abstracts away OS-specific shell invocation (sh on Unix,
 * cmd.exe on Windows) and handles pipe chains (cmd1 | cmd2) by splitting
 * them into multiple QProcess instances connected via stdout→stdin.
 */

#include <QObject>
#include <QProcess>
#include <QStringList>

/**
 * @class ProcessRunner
 * @brief Runs shell commands cross-platform with automatic pipe chain handling.
 *
 * On Unix systems, commands are executed via `sh -c`.
 * On Windows, commands are executed via `cmd.exe /c`.
 *
 * When a command contains pipe characters (`|`), the command is split into
 * separate QProcess instances. The stdout of each process is connected to
 * the stdin of the next, achieving zero-copy piping without relying on a
 * shell's pipe implementation.
 *
 * @note Pipe splitting respects quoted strings (single and double quotes).
 *
 * Example usage:
 * @code
 * auto *runner = new ProcessRunner(this);
 * connect(runner, &ProcessRunner::outputReady, this, &MyClass::handleOutput);
 * connect(runner, &ProcessRunner::finished, this, &MyClass::handleDone);
 * runner->run("ffmpeg -i input.mkv -c:v copy -f hevc - | dovi_tool extract-rpu - -o rpu.bin");
 * @endcode
 */
class ProcessRunner : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Constructs a ProcessRunner.
     * @param parent Optional parent QObject for memory management.
     */
    explicit ProcessRunner(QObject *parent = nullptr);

    /**
     * @brief Execute a command string.
     *
     * If the command contains pipes (`|`), it is split into multiple
     * processes connected via stdout→stdin. Otherwise a single process
     * is launched.
     *
     * @param command The full command line to execute.
     */
    void run(const QString &command);

    /**
     * @brief Kill all running processes in the chain.
     */
    void abort();

    /**
     * @brief Returns the last process in the pipe chain.
     * @return Pointer to the last QProcess, or nullptr if no processes exist.
     */
    QProcess* lastProcess() const { return m_processes.isEmpty() ? nullptr : m_processes.last(); }

signals:
    /**
     * @brief Emitted when all processes in the chain have completed.
     * @param exitCode The exit code of the last process in the chain.
     */
    void finished(int exitCode);

    /**
     * @brief Emitted when stderr or stdout data is available from any process.
     * @param data The output text (may be partial lines).
     */
    void outputReady(const QString &data);

private:
    QList<QProcess*> m_processes; ///< All processes in the current pipe chain.

    /** @brief Delete and clear all process instances. */
    void cleanup();

    /**
     * @brief Split a command string on unquoted pipe characters.
     * @param command The full command string.
     * @return List of individual commands (one per pipe segment).
     */
    static QStringList splitPipe(const QString &command);

    /**
     * @brief Configure a QProcess with the platform-appropriate shell.
     * @param proc The process to configure.
     * @param cmd The command to execute within the shell.
     */
    static void startProcess(QProcess *proc, const QString &cmd);
};
