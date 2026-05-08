#include "doviprocessor.h"
#include "processrunner.h"

DoviProcessor::DoviProcessor(QObject *parent) : QObject(parent) {}

void DoviProcessor::extractRpu(const QString &cmd) { runCommand(cmd); }
void DoviProcessor::convertRpu(const QString &cmd) { runCommand(cmd); }
void DoviProcessor::injectRpu(const QString &cmd) { runCommand(cmd); }

void DoviProcessor::abort()
{
    if (m_runner) {
        m_runner->abort();
    }
}

void DoviProcessor::runCommand(const QString &cmd)
{
    m_runner = new ProcessRunner(this);
    connect(m_runner, &ProcessRunner::outputReady, this, &DoviProcessor::logOutput);
    connect(m_runner, &ProcessRunner::finished, this, [this](int exitCode) {
        emit stepFinished(exitCode == 0);
        m_runner->deleteLater();
        m_runner = nullptr;
    });
    m_runner->run(cmd);
}
