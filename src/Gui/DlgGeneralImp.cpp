/***************************************************************************
 *   Copyright (c) 2004 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/


#include "PreCompiled.h"
#ifndef _PreComp_
# include <QApplication>
# include <QLocale>
# include <QStyleFactory>
# include <QTextStream>
# include <QDesktopServices>
#endif

#include "DlgGeneralImp.h"
#include "ui_DlgGeneral.h"
#include "Action.h"
#include "Application.h"
#include "Command.h"
#include "DockWindowManager.h"
#include "MainWindow.h"
#include "PrefWidgets.h"
#include "PythonConsole.h"
#include "Language/Translator.h"
#include "Gui/PreferencePackManager.h"
#include "DlgPreferencesImp.h"

#include "DlgCreateNewPreferencePackImp.h"

// Only needed until PreferencePacks can be managed from the AddonManager:
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;


using namespace Gui::Dialog;

/* TRANSLATOR Gui::Dialog::DlgGeneralImp */

/**
 *  Constructs a DlgGeneralImp which is a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'
 *
 *  The dialog will by default be modeless, unless you set 'modal' to
 *  true to construct a modal dialog.
 */
DlgGeneralImp::DlgGeneralImp( QWidget* parent )
  : PreferencePage(parent)
  , ui(new Ui_DlgGeneral)
{
    ui->setupUi(this);

    // fills the combo box with all available workbenches
    // sorted by their menu text
    QStringList work = Application::Instance->workbenches();
    QMap<QString, QString> menuText;
    for (QStringList::Iterator it = work.begin(); it != work.end(); ++it) {
        QString text = Application::Instance->workbenchMenuText(*it);
        menuText[text] = *it;
    }

    {   // add special workbench to selection
        QPixmap px = Application::Instance->workbenchIcon(QString::fromLatin1("NoneWorkbench"));
        QString key = QString::fromLatin1("<last>");
        QString value = QString::fromLatin1("$LastModule");
        if (px.isNull())
            ui->AutoloadModuleCombo->addItem(key, QVariant(value));
        else
            ui->AutoloadModuleCombo->addItem(px, key, QVariant(value));
    }

    for (QMap<QString, QString>::Iterator it = menuText.begin(); it != menuText.end(); ++it) {
        QPixmap px = Application::Instance->workbenchIcon(it.value());
        if (px.isNull())
            ui->AutoloadModuleCombo->addItem(it.key(), QVariant(it.value()));
        else
            ui->AutoloadModuleCombo->addItem(px, it.key(), QVariant(it.value()));
    }

    recreatePreferencePackMenu();
    connect(ui->SaveNewPreferencePack, &QPushButton::clicked, this, &DlgGeneralImp::saveAsNewPreferencePack);

    // Future work: the Add-On Manager will be modified to include a section for Preference Packs, at which point this
    // button will be modified to open the Add-On Manager to that tab.
    auto savedPreferencePacksDirectory = fs::path(App::Application::getUserAppDataDir()) / "SavedPreferencePacks";

    // If that directory hasn't been created yet, just send the user to the preferences directory
    if (!(fs::exists(savedPreferencePacksDirectory) && fs::is_directory(savedPreferencePacksDirectory))) {
        savedPreferencePacksDirectory = fs::path(App::Application::getUserAppDataDir());
        ui->ManagePreferencePacks->hide();
    }
    
    QString pathToSavedPacks(QString::fromStdString(savedPreferencePacksDirectory.string()));
    ui->ManagePreferencePacks->setToolTip(tr("Open the directory of saved user preference packs"));
    connect(ui->ManagePreferencePacks, &QPushButton::clicked, this, [pathToSavedPacks]() { QDesktopServices::openUrl(QUrl::fromLocalFile(pathToSavedPacks)); });
}

/**
 *  Destroys the object and frees any allocated resources
 */
DlgGeneralImp::~DlgGeneralImp()
{
}

/** Sets the size of the recent file list from the user parameters.
 * @see RecentFilesAction
 * @see StdCmdRecentFiles
 */
void DlgGeneralImp::setRecentFileSize()
{
    RecentFilesAction *recent = getMainWindow()->findChild<RecentFilesAction *>(QLatin1String("recentFiles"));
    if (recent) {
        ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("RecentFiles");
        recent->resizeList(hGrp->GetInt("RecentFiles", 4));
    }
}

void DlgGeneralImp::saveSettings()
{
    int index = ui->AutoloadModuleCombo->currentIndex();
    QVariant data = ui->AutoloadModuleCombo->itemData(index);
    QString startWbName = data.toString();
    App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")->
                          SetASCII("AutoloadModule", startWbName.toLatin1());

    ui->RecentFiles->onSave();
    ui->SplashScreen->onSave();
    ui->PythonWordWrap->onSave();

    QWidget* pc = DockWindowManager::instance()->getDockWindow("Python console");
    PythonConsole *pcPython = qobject_cast<PythonConsole*>(pc);
    if (pcPython) {
        bool pythonWordWrap = App::GetApplication().GetUserParameter().
            GetGroup("BaseApp")->GetGroup("Preferences")->GetGroup("General")->GetBool("PythonWordWrap", true);

        if (pythonWordWrap) {
            pcPython->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        } else {
            pcPython->setWordWrapMode(QTextOption::NoWrap);
        }
    }

    setRecentFileSize();
    ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("General");
    QString lang = QLocale::languageToString(QLocale().language());
    QByteArray language = hGrp->GetASCII("Language", (const char*)lang.toLatin1()).c_str();
    QByteArray current = ui->Languages->itemData(ui->Languages->currentIndex()).toByteArray();
    if (current != language) {
        hGrp->SetASCII("Language", current.constData());
        Translator::instance()->activateLanguage(current.constData());
    }

    QVariant size = ui->toolbarIconSize->itemData(ui->toolbarIconSize->currentIndex());
    int pixel = size.toInt();
    hGrp->SetInt("ToolbarIconSize", pixel);
    getMainWindow()->setIconSize(QSize(pixel,pixel));

    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/DockWindows");
    bool treeView=false, propertyView=false, comboView=true;
    switch(ui->treeMode->currentIndex()) {
    case 1:
        treeView = propertyView = true;
        comboView = false;
        break;
    case 2:
        comboView = true;
        treeView = propertyView = true;
        break;
    }
    hGrp->GetGroup("ComboView")->SetBool("Enabled",comboView);
    hGrp->GetGroup("TreeView")->SetBool("Enabled",treeView);
    hGrp->GetGroup("PropertyView")->SetBool("Enabled",propertyView);

    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
    hGrp->SetBool("TiledBackground", ui->tiledBackground->isChecked());

    QVariant sheet = ui->StyleSheets->itemData(ui->StyleSheets->currentIndex());
    hGrp->SetASCII("StyleSheet", (const char*)sheet.toByteArray());
    Application::Instance->setStyleSheet(sheet.toString(), ui->tiledBackground->isChecked());
}

void DlgGeneralImp::loadSettings()
{
    std::string start = App::Application::Config()["StartWorkbench"];
    start = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/General")->
                                  GetASCII("AutoloadModule", start.c_str());
    QString startWbName = QLatin1String(start.c_str());
    ui->AutoloadModuleCombo->setCurrentIndex(ui->AutoloadModuleCombo->findData(startWbName));

    ui->RecentFiles->onRestore();
    ui->SplashScreen->onRestore();
    ui->PythonWordWrap->onRestore();

    // search for the language files
    ParameterGrp::handle hGrp = WindowParameter::getDefaultParameter()->GetGroup("General");
    QString langToStr = QLocale::languageToString(QLocale().language());
    QByteArray language = hGrp->GetASCII("Language", langToStr.toLatin1()).c_str();

    int index = 1;
    TStringMap list = Translator::instance()->supportedLocales();
    ui->Languages->clear();
    ui->Languages->addItem(QString::fromLatin1("English"), QByteArray("English"));
    for (TStringMap::iterator it = list.begin(); it != list.end(); ++it, index++) {
        QByteArray lang = it->first.c_str();
        QString langname = QString::fromLatin1(lang.constData());

        QLocale locale(QString::fromLatin1(it->second.c_str()));
        QString native = locale.nativeLanguageName();
        if (!native.isEmpty()) {
            if (native[0].isLetter())
                native[0] = native[0].toUpper();
            langname = native;
        }

        ui->Languages->addItem(langname, lang);
        if (language == lang) {
            ui->Languages->setCurrentIndex(index);
        }
    }

    QAbstractItemModel* model = ui->Languages->model();
    if (model)
        model->sort(0);

    int current = getMainWindow()->iconSize().width();
    ui->toolbarIconSize->addItem(tr("Small (%1px)").arg(16), QVariant((int)16));
    ui->toolbarIconSize->addItem(tr("Medium (%1px)").arg(24), QVariant((int)24));
    ui->toolbarIconSize->addItem(tr("Large (%1px)").arg(32), QVariant((int)32));
    ui->toolbarIconSize->addItem(tr("Extra large (%1px)").arg(48), QVariant((int)48));
    index = ui->toolbarIconSize->findData(QVariant(current));
    if (index < 0) {
        ui->toolbarIconSize->addItem(tr("Custom (%1px)").arg(current), QVariant((int)current));
        index = ui->toolbarIconSize->findData(QVariant(current));
    }
    ui->toolbarIconSize->setCurrentIndex(index);

    ui->treeMode->addItem(tr("Combo View"));
    ui->treeMode->addItem(tr("TreeView and PropertyView"));
    ui->treeMode->addItem(tr("Both"));

    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/DockWindows");
    bool propertyView = hGrp->GetGroup("PropertyView")->GetBool("Enabled",false);
    bool treeView = hGrp->GetGroup("TreeView")->GetBool("Enabled",false);
    bool comboView = hGrp->GetGroup("ComboView")->GetBool("Enabled",true);
    index = 0;
    if(propertyView || treeView) {
        index = comboView?2:1;
    }
    ui->treeMode->setCurrentIndex(index);

    hGrp = App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/MainWindow");
    ui->tiledBackground->setChecked(hGrp->GetBool("TiledBackground", false));

    // List all .qss/.css files
    QMap<QString, QString> cssFiles;
    QDir dir;
    QStringList filter;
    filter << QString::fromLatin1("*.qss");
    filter << QString::fromLatin1("*.css");
    QFileInfoList fileNames;

    // read from user, resource and built-in directory
    QStringList qssPaths = QDir::searchPaths(QString::fromLatin1("qss"));
    for (QStringList::iterator it = qssPaths.begin(); it != qssPaths.end(); ++it) {
        dir.setPath(*it);
        fileNames = dir.entryInfoList(filter, QDir::Files, QDir::Name);
        for (QFileInfoList::iterator jt = fileNames.begin(); jt != fileNames.end(); ++jt) {
            if (cssFiles.find(jt->baseName()) == cssFiles.end()) {
                cssFiles[jt->baseName()] = jt->fileName();
            }
        }
    }

    // now add all unique items
    ui->StyleSheets->addItem(tr("No style sheet"), QString::fromLatin1(""));
    for (QMap<QString, QString>::iterator it = cssFiles.begin(); it != cssFiles.end(); ++it) {
        ui->StyleSheets->addItem(it.key(), it.value());
    }

    QString selectedStyleSheet = QString::fromLatin1(hGrp->GetASCII("StyleSheet").c_str());
    index = ui->StyleSheets->findData(selectedStyleSheet);

    // might be an absolute path name
    if (index < 0 && !selectedStyleSheet.isEmpty()) {
        QFileInfo fi(selectedStyleSheet);
        if (fi.isAbsolute()) {
            QString path = fi.absolutePath();
            if (qssPaths.indexOf(path) >= 0) {
                selectedStyleSheet = fi.fileName();
            }
            else {
                selectedStyleSheet = fi.absoluteFilePath();
                ui->StyleSheets->addItem(fi.baseName(), selectedStyleSheet);
            }

            index = ui->StyleSheets->findData(selectedStyleSheet);
        }
    }

    if (index > -1) ui->StyleSheets->setCurrentIndex(index);
}

void DlgGeneralImp::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    else {
        QWidget::changeEvent(e);
    }
}

void DlgGeneralImp::recreatePreferencePackMenu()
{
    ui->PreferencePacks->setRowCount(0); // Begin by clearing whatever is there
    ui->PreferencePacks->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    ui->PreferencePacks->setColumnCount(3);
    ui->PreferencePacks->setSelectionMode(QAbstractItemView::SelectionMode::NoSelection);
    ui->PreferencePacks->horizontalHeader()->setStretchLastSection(false);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::Stretch);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeMode::Stretch);
    ui->PreferencePacks->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
    QStringList columnHeaders;
    columnHeaders << tr("Preference Pack Name") 
                  << tr("Tags")
                  << QString(); // for the "Load" buttons
    ui->PreferencePacks->setHorizontalHeaderLabels(columnHeaders);

    // Populate the Preference Packs list
    auto packs = Application::Instance->prefPackManager()->preferencePacks();

    ui->PreferencePacks->setRowCount(packs.size());

    int row = 0;
    QIcon icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
    for (const auto& pack : packs) {
        auto name = new QTableWidgetItem(QString::fromStdString(pack.first));
        name->setToolTip(QString::fromStdString(pack.second.metadata().description()));
        ui->PreferencePacks->setItem(row, 0, name);
        auto tags = pack.second.metadata().tag();
        QString tagString;
        for (const auto& tag : tags) {
            if (tagString.isEmpty())
                tagString.append(QString::fromStdString(tag));
            else
                tagString.append(QStringLiteral(", ") + QString::fromStdString(tag));
        }
        QTableWidgetItem* kind = new QTableWidgetItem(tagString);
        ui->PreferencePacks->setItem(row, 1, kind);
        auto button = new QPushButton(icon, tr("Apply"));
        button->setToolTip(tr("Apply the %1 preference pack").arg(QString::fromStdString(pack.first)));
        connect(button, &QPushButton::clicked, this, [this, pack]() { onLoadPreferencePackClicked(pack.first); });
        ui->PreferencePacks->setCellWidget(row, 2, button);
        ++row;
    }
}

void DlgGeneralImp::saveAsNewPreferencePack()
{
    // Create and run a modal New PreferencePack dialog box
    auto packs = Application::Instance->prefPackManager()->preferencePackNames();
    newPreferencePackDialog = std::make_unique<DlgCreateNewPreferencePackImp>(this);
    newPreferencePackDialog->setPreferencePackTemplates(Application::Instance->prefPackManager()->templateFiles());
    newPreferencePackDialog->setPreferencePackNames(packs);
    connect(newPreferencePackDialog.get(), &DlgCreateNewPreferencePackImp::accepted, this, &DlgGeneralImp::newPreferencePackDialogAccepted);
    newPreferencePackDialog->open();
}

void DlgGeneralImp::newPreferencePackDialogAccepted() 
{
    auto preferencePackTemplates = Application::Instance->prefPackManager()->templateFiles();
    auto selection = newPreferencePackDialog->selectedTemplates();
    std::vector<PreferencePackManager::TemplateFile> selectedTemplates;
    std::copy_if(preferencePackTemplates.begin(), preferencePackTemplates.end(), std::back_inserter(selectedTemplates), [selection](PreferencePackManager::TemplateFile& t) {
        for (const auto& item : selection)
            if (item.group == t.group && item.name == t.name)
                return true;
        return false;
        });
    auto preferencePackName = newPreferencePackDialog->preferencePackName();
    Application::Instance->prefPackManager()->save(preferencePackName, selectedTemplates);
    Application::Instance->prefPackManager()->rescan();
    recreatePreferencePackMenu();
}

void DlgGeneralImp::onLoadPreferencePackClicked(const std::string& packName)
{
    if (Application::Instance->prefPackManager()->apply(packName)) {
        auto parentDialog = qobject_cast<DlgPreferencesImp*> (this->window());
        if (parentDialog)
            parentDialog->reload();
    }
}


#include "moc_DlgGeneralImp.cpp"
