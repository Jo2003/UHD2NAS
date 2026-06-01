#pragma once

/**
 * @file settingsdialog.h
 * @brief Dialog for configuring external tool paths and command templates.
 */

#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QTableWidget>
#include <QLabel>
#include <QSyntaxHighlighter>
#include "templatemanager.h"

/**
 * @class PlaceholderHighlighter
 * @brief Highlights {placeholder} tokens in violet.
 */
class PlaceholderHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit PlaceholderHighlighter(QTextDocument *parent);
protected:
    void highlightBlock(const QString &text) override;
};

/**
 * @class SettingsDialog
 * @brief Configuration dialog for tool paths, download links, and command templates.
 */
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(TemplateManager *templates, QWidget *parent = nullptr);

    QString ffmpegPath() const;
    QString ffprobePath() const;
    QString doviToolPath() const;
    QString mkvmergePath() const;

    void setFfmpegPath(const QString &p);
    void setFfprobePath(const QString &p);
    void setDoviToolPath(const QString &p);
    void setMkvmergePath(const QString &p);

private:
    QLineEdit *m_ffmpegEdit;
    QLineEdit *m_ffprobeEdit;
    QLineEdit *m_doviToolEdit;
    QLineEdit *m_mkvmergeEdit;
    QTableWidget *m_templateTable;
    QTextEdit *m_templateEdit;
    QLabel *m_placeholderLabel;
    TemplateManager *m_templates;
    int m_currentTemplateRow = -1;

    void browse(QLineEdit *edit);
    void populateTemplates();
    void resetTemplates();
    void applyTemplates();
    void onTemplateSelected(int row);
    void saveCurrentTemplate();
};
