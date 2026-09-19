/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */


#include "PreferencesDialog.h"
#include "NotepadNextApplication.h"
#include "TranslationManager.h"
#include "ui_PreferencesDialog.h"
#include "ScintillaNext.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QDir>
#include <QStandardPaths>


PreferencesDialog::PreferencesDialog(ApplicationSettings *settings, QWidget *parent) :
    QDialog(parent, Qt::Dialog),
    ui(new Ui::PreferencesDialog),
    settings(settings)
{
    ui->setupUi(this);

    // Generate a checkmark glyph so the box shows a real tick instead of a filled color
    const QString checkmarkPath = QDir::tempPath() + "/notepadnext-checkmark.png";
    QPixmap checkmark(16, 16);
    checkmark.fill(Qt::transparent);
    {
        QPainter painter(&checkmark);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#111111"), 2));
        painter.drawLine(QPointF(3, 9), QPointF(7, 13));
        painter.drawLine(QPointF(7, 13), QPointF(13, 4));
    }
    checkmark.save(checkmarkPath);

    // Generate a filled dot glyph for the radio button selection
    const QString dotPath = QDir::tempPath() + "/notepadnext-radio-dot.png";
    QPixmap dot(16, 16);
    dot.fill(Qt::transparent);
    {
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QBrush(QColor("#111111")));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(8, 8), 4.0, 4.0);
    }
    dot.save(dotPath);

    setStyleSheet(QStringLiteral(
        "QDialog#PreferencesDialog { background-color: #ffffff; color: #000000; }"
        "QDialog#PreferencesDialog QScrollArea { background-color: #ffffff; border: none; }"
        "QDialog#PreferencesDialog QScrollArea > QWidget > QWidget { background-color: #ffffff; }"
        "QDialog#PreferencesDialog QScrollArea QWidget { background: #ffffff; }"
        "QDialog#PreferencesDialog QGroupBox { background-color: #f0f0f0; color: #000000; border: 1px solid #c0c0c0; border-radius: 4px; }"
        "QDialog#PreferencesDialog QGroupBox::indicator { width: 16px; height: 16px; border: 1px solid #333333; border-radius: 3px; background-color: #ffffff; margin-right: 4px; }"
        "QDialog#PreferencesDialog QGroupBox::indicator:checked { image: url('%1'); background-color: #ffffff; border: 1px solid #333333; }"
        "QDialog#PreferencesDialog QWidget { color: #000000; }"
        "QDialog#PreferencesDialog QLabel { color: #000000; background: transparent; }"
        "QDialog#PreferencesDialog QCheckBox { color: #000000; background: transparent; spacing: 8px; }"
        "QDialog#PreferencesDialog QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #333333; border-radius: 3px; background-color: #ffffff; }"
        "QDialog#PreferencesDialog QCheckBox::indicator:checked { image: url('%1'); background-color: #ffffff; border: 1px solid #333333; }"
        "QDialog#PreferencesDialog QCheckBox::indicator:hover { border-color: #2d8ff0; }"
        "QDialog#PreferencesDialog QRadioButton { color: #000000; background: transparent; spacing: 8px; }"
        "QDialog#PreferencesDialog QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid #333333; border-radius: 8px; background-color: #ffffff; }"
        "QDialog#PreferencesDialog QRadioButton::indicator:checked { image: url('%2'); background-color: #ffffff; border: 1px solid #333333; }"
        "QDialog#PreferencesDialog QLineEdit, QDialog#PreferencesDialog QComboBox, QDialog#PreferencesDialog QSpinBox, QDialog#PreferencesDialog QFontComboBox { background-color: #ffffff; color: #000000; border: 1px solid #b0b0b0; }"
        "QDialog#PreferencesDialog QPushButton, QDialog#PreferencesDialog QToolButton { background-color: #f0f0f0; color: #000000; border: 1px solid #b0b0b0; border-radius: 3px; padding: 3px 8px; }"
        "QDialog#PreferencesDialog QScrollBar:vertical { background: #f0f0f0; width: 14px; }"
        "QDialog#PreferencesDialog QScrollBar::handle:vertical { background: #c0c0c0; border-radius: 7px; min-height: 20px; }").arg(checkmarkPath, dotPath));

    QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
    QPixmap pixmap = icon.pixmap(QSize(16, 16));
    ui->labelAppRestartIcon->setPixmap(pixmap);
    ui->labelAppRestartIcon->hide();
    ui->labelAppRestart->hide();

    MapSettingToCheckBox(ui->checkBoxMenuBar, &ApplicationSettings::showMenuBar, &ApplicationSettings::setShowMenuBar, &ApplicationSettings::showMenuBarChanged);
    MapSettingToCheckBox(ui->checkBoxToolBar, &ApplicationSettings::showToolBar, &ApplicationSettings::setShowToolBar, &ApplicationSettings::showToolBarChanged);
    MapSettingToCheckBox(ui->checkBoxStatusBar, &ApplicationSettings::showStatusBar, &ApplicationSettings::setShowStatusBar, &ApplicationSettings::showStatusBarChanged);
    MapSettingToCheckBox(ui->checkBoxRecenterSearchDialog, &ApplicationSettings::centerSearchDialog, &ApplicationSettings::setCenterSearchDialog, &ApplicationSettings::centerSearchDialogChanged);

    MapSettingToGroupBox(ui->gbxRestorePreviousSession, &ApplicationSettings::restorePreviousSession, &ApplicationSettings::setRestorePreviousSession, &ApplicationSettings::restorePreviousSessionChanged);
    connect(ui->gbxRestorePreviousSession, &QGroupBox::toggled, this, [=](bool checked) {
        if (!checked) {
            ui->checkBoxUnsavedFiles->setChecked(false);
            ui->checkBoxRestoreTempFiles->setChecked(false);
        }
        else {
            QMessageBox::warning(this, tr("Warning"), tr("This feature is experimental and it should not be considered safe for critically important work. It may lead to possible data loss. Use at your own risk."));
        }
    });

    MapSettingToCheckBox(ui->checkBoxUnsavedFiles, &ApplicationSettings::restoreUnsavedFiles, &ApplicationSettings::setRestoreUnsavedFiles, &ApplicationSettings::restoreUnsavedFilesChanged);
    MapSettingToCheckBox(ui->checkBoxRestoreTempFiles, &ApplicationSettings::restoreTempFiles, &ApplicationSettings::setRestoreTempFiles, &ApplicationSettings::restoreTempFilesChanged);

    MapSettingToCheckBox(ui->checkBoxCombineSearchResults, &ApplicationSettings::combineSearchResults, &ApplicationSettings::setCombineSearchResults, &ApplicationSettings::combineSearchResultsChanged);

    populateTranslationComboBox();
    connect(ui->comboBoxTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        settings->setTranslation(ui->comboBoxTranslation->itemData(index).toString());
        showApplicationRestartRequired();
    });

    MapSettingToCheckBox(ui->checkBoxExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosed, &ApplicationSettings::setExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosedChanged);

    ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    connect(ui->fcbDefaultFont, &QFontComboBox::currentFontChanged, this, [=](const QFont &f) {
        settings->setFontName(f.family());
    });
    connect(settings, &ApplicationSettings::fontNameChanged, this, [=](QString fontName){
        ui->fcbDefaultFont->setCurrentFont(QFont(fontName));
    });

    ui->spbDefaultFontSize->setValue(settings->fontSize());
    connect(ui->spbDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), settings, &ApplicationSettings::setFontSize);
    connect(settings, &ApplicationSettings::fontSizeChanged, ui->spbDefaultFontSize, &QSpinBox::setValue);

    ui->comboBoxLineEndings->addItem(tr("System Default"), QString(""));
    ui->comboBoxLineEndings->addItem(tr("Windows (CR LF)"), ScintillaNext::eolModeToString(SC_EOL_CRLF));
    ui->comboBoxLineEndings->addItem(tr("Linux (LF)"), ScintillaNext::eolModeToString(SC_EOL_LF));
    ui->comboBoxLineEndings->addItem(tr("Macintosh (CR)"), ScintillaNext::eolModeToString(SC_EOL_CR));

    // Select the current one
    int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
    ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);

    connect(ui->comboBoxLineEndings, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        settings->setDefaultEOLMode(ui->comboBoxLineEndings->itemData(index).toString());
    });
    connect(settings, &ApplicationSettings::defaultEOLModeChanged, this, [=](QString defaultEOLMode) {
        int index = ui->comboBoxLineEndings->findData(defaultEOLMode);
        ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);
    });

    MapSettingToCheckBox(ui->checkBoxHighlightURLs, &ApplicationSettings::urlHighlighting, &ApplicationSettings::setURLHighlighting, &ApplicationSettings::urlHighlightingChanged);
    MapSettingToCheckBox(ui->checkBoxShowLineNumbers, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers, &ApplicationSettings::showLineNumbersChanged);


    QButtonGroup *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(ui->radioFollowCurrentDirectory, ApplicationSettings::FollowCurrentDocument);
    buttonGroup->addButton(ui->radioLastUsedDirectory, ApplicationSettings::RememberLastUsed);
    buttonGroup->addButton(ui->radioHardCoded, ApplicationSettings::HardCoded);

    connect(buttonGroup, &QButtonGroup::idClicked, this, [=](int id) {
        ApplicationSettings::DefaultDirectoryBehaviorEnum e = static_cast<ApplicationSettings::DefaultDirectoryBehaviorEnum>(id);
        settings->setDefaultDirectoryBehavior(e);
    });

    connect(ui->radioHardCoded, &QRadioButton::toggled, this, [=](bool checked){
        ui->btnSelectHardCodedPath->setEnabled(checked);
        ui->txtHardCodedPath->setEnabled(checked);
    });

    connect(ui->btnSelectHardCodedPath, &QToolButton::clicked, this, [=]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Default Directory"));
        if (dir.isEmpty()) return; // user cancelled

        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    connect(ui->txtHardCodedPath, &QLineEdit::editingFinished, this, [=]() {
        QString dir = ui->txtHardCodedPath->text();
        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    if (auto b = buttonGroup->button(settings->defaultDirectoryBehavior())) {
        b->setChecked(true);
    }

    if (settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded) {
        ui->txtHardCodedPath->setText((QDir::toNativeSeparators(settings->defaultDirectory())));
    }
    else {
        ui->txtHardCodedPath->setText(QString());
    }
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

void PreferencesDialog::showApplicationRestartRequired() const
{
    ui->labelAppRestartIcon->show();
    ui->labelAppRestart->show();
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToCheckBox(QCheckBox *checkBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    checkBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, checkBox, &QCheckBox::setChecked);
    connect(checkBox, &QCheckBox::toggled, settings, setter);
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToGroupBox(QGroupBox *groupBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    groupBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, groupBox, &QGroupBox::setChecked);
    connect(groupBox, &QGroupBox::toggled, settings, setter);
}

void PreferencesDialog::populateTranslationComboBox()
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);

    // Add the system default at the top
    ui->comboBoxTranslation->addItem(tr("<System Default>"), QStringLiteral(""));

    // TODO: sort this list and keep the system default at the top
    for (const auto &localeName : app->getTranslationManager()->availableTranslations())
    {
        QLocale locale(localeName);
        const QString localeDisplay = TranslationManager::FormatLocaleTerritoryAndLanguage(locale);
        ui->comboBoxTranslation->addItem(localeDisplay, localeName);
    }

    // Select the current one
    int index = ui->comboBoxTranslation->findData(settings->translation());
    if (index != -1) {
        ui->comboBoxTranslation->setCurrentIndex(index);
    }
}
