/***************************************************************************
 *   Copyright (c) Eivind Kvedalen (eivind@kvedalen.name) 2015-2016        *
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
# include <QAction>
# include <QApplication>
# include <QMenu>
# include <QMouseEvent>
# include <QSlider>
# include <QStatusBar>
# include <QToolBar>
# include <QTableWidgetItem>
# include <QMessageBox>
# include <QPalette>
# include <cmath>
#endif

#include <App/DocumentObject.h>
#include <App/PropertyStandard.h>
#include <App/Range.h>
#include <Base/Tools.h>
#include <boost_bind_bind.hpp>
#include <Gui/MainWindow.h>
#include <Gui/Application.h>
#include <Gui/Command.h>
#include <Gui/CommandT.h>
#include <Gui/Document.h>
#include <Gui/ExpressionCompleter.h>
#include <LineEdit.h>
#include <Mod/Spreadsheet/App/Sheet.h>
#include <Mod/Spreadsheet/App/SheetPy.h>
#include <Mod/Spreadsheet/App/Utils.h>
#include "qtcolorpicker.h"

#include "SpreadsheetView.h"
#include "SpreadsheetViewPy.h"
#include "SpreadsheetDelegate.h"
#include "ui_Sheet.h"

using namespace SpreadsheetGui;
using namespace Spreadsheet;
using namespace Gui;
using namespace App;
namespace bp = boost::placeholders;

/* TRANSLATOR SpreadsheetGui::SheetView */

TYPESYSTEM_SOURCE_ABSTRACT(SpreadsheetGui::SheetView, Gui::MDIView)

SheetView::SheetView(Gui::Document *pcDocument, App::DocumentObject *docObj, QWidget *parent)
    : MDIView(pcDocument, parent)
    , sheet(static_cast<Sheet*>(docObj))
{
    // Set up ui

    model = new SheetModel(static_cast<Sheet*>(docObj));

    ui = new Ui::Sheet();
    QWidget * w = new QWidget(this);
    ui->setupUi(w);
    setCentralWidget(w);

    delegate = new SpreadsheetDelegate(sheet);
    ui->cells->setModel(model);
    ui->cells->setItemDelegate(delegate);
    ui->cells->setSheet(sheet);

    // Connect signals
    connect(ui->cells->selectionModel(), SIGNAL( currentChanged( QModelIndex, QModelIndex ) ),
            this,        SLOT( currentChanged( QModelIndex, QModelIndex ) ) );

    connect(ui->cells->horizontalHeader(), SIGNAL(resizeFinished()),
            this, SLOT(columnResizeFinished()));
    connect(ui->cells->horizontalHeader(), SIGNAL(sectionResized ( int, int, int ) ),
            this, SLOT(columnResized(int, int, int)));

    connect(ui->cells->verticalHeader(), SIGNAL(resizeFinished()),
            this, SLOT(rowResizeFinished()));
    connect(ui->cells->verticalHeader(), SIGNAL(sectionResized ( int, int, int ) ),
            this, SLOT(rowResized(int, int, int)));

    connect(delegate, &SpreadsheetDelegate::finishedWithKey, this, &SheetView::editingFinishedWithKey);
    connect(ui->cellContent, &ExpressionLineEdit::returnPressed, this, [this]() {confirmContentChanged(ui->cellContent->text()); });
    connect(ui->cellAlias, &ExpressionLineEdit::returnPressed, this, [this]() {confirmAliasChanged(ui->cellAlias->text()); });
    connect(ui->cellAlias, &LineEdit::textEdited, this, &SheetView::aliasChanged);

    columnWidthChangedConnection = sheet->columnWidthChanged.connect(bind(&SheetView::resizeColumn, this, bp::_1, bp::_2));
    rowHeightChangedConnection = sheet->rowHeightChanged.connect(bind(&SheetView::resizeRow, this, bp::_1, bp::_2));

    connect( model, SIGNAL(dataChanged(const QModelIndex &, const QModelIndex &)), this, SLOT(modelUpdated(const QModelIndex &, const QModelIndex &)));

    QPalette palette = ui->cells->palette();
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(0, 0, 0));
    ui->cells->setPalette(palette);

    QList<QtColorPicker*> bgList = Gui::getMainWindow()->findChildren<QtColorPicker*>(QString::fromLatin1("Spreadsheet_BackgroundColor"));
    if (bgList.size() > 0)
        bgList[0]->setCurrentColor(palette.color(QPalette::Base));

    QList<QtColorPicker*> fgList = Gui::getMainWindow()->findChildren<QtColorPicker*>(QString::fromLatin1("Spreadsheet_ForegroundColor"));
    if (fgList.size() > 0)
        fgList[0]->setCurrentColor(palette.color(QPalette::Text));

    // Set document object to create auto completer
    ui->cellContent->setDocumentObject(sheet);
    ui->cellAlias->setDocumentObject(sheet);
}

SheetView::~SheetView()
{
    Gui::Application::Instance->detachView(this);
    //delete delegate;
}

bool SheetView::onMsg(const char *pMsg, const char **)
{
    if(strcmp("Undo",pMsg) == 0 ) {
        getGuiDocument()->undo(1);
        App::Document* doc = getAppDocument();
        if (doc)
            doc->recompute();
        return true;
    }
    else  if(strcmp("Redo",pMsg) == 0 ) {
        getGuiDocument()->redo(1);
        App::Document* doc = getAppDocument();
        if (doc)
            doc->recompute();
        return true;
    }
    else if (strcmp("Save",pMsg) == 0) {
        getGuiDocument()->save();
        return true;
    }
    else if (strcmp("SaveAs",pMsg) == 0) {
        getGuiDocument()->saveAs();
        return true;
    }
    else if(strcmp("Std_Delete",pMsg) == 0) {
        std::vector<Range> ranges = selectedRanges();
        if (sheet->hasCell(ranges)) {
            Gui::Command::openCommand(QT_TRANSLATE_NOOP("Command", "Clear cell(s)"));
            std::vector<Range>::const_iterator i = ranges.begin();
            for (; i != ranges.end(); ++i) {
                FCMD_OBJ_CMD(sheet, "clear('" << i->rangeString() << "')");
            }
            Gui::Command::commitCommand();
            Gui::Command::doCommand(Gui::Command::Doc, "App.ActiveDocument.recompute()");
        }
        return true;
    }
    else if (strcmp("Cut",pMsg) == 0) {
        ui->cells->cutSelection();
        return true;
    }
    else if (strcmp("Copy",pMsg) == 0) {
        ui->cells->copySelection();
        return true;
    }
    else if (strcmp("Paste",pMsg) == 0) {
        ui->cells->pasteClipboard();
        return true;
    }
    else
        return false;
}

bool SheetView::onHasMsg(const char *pMsg) const
{
    if (strcmp("Undo",pMsg) == 0) {
        App::Document* doc = getAppDocument();
        return doc && doc->getAvailableUndos() > 0;
    }
    else if (strcmp("Redo",pMsg) == 0) {
        App::Document* doc = getAppDocument();
        return doc && doc->getAvailableRedos() > 0;
    }
    else if  (strcmp("Save",pMsg) == 0)
        return true;
    else if (strcmp("SaveAs",pMsg) == 0)
        return true;
    else if (strcmp("Cut",pMsg) == 0)
        return true;
    else if (strcmp("Copy",pMsg) == 0)
        return true;
    else if (strcmp("Paste",pMsg) == 0)
        return true;
    else
        return false;
}

void SheetView::setCurrentCell(QString str)
{
    Q_UNUSED(str);
    updateContentLine();
    updateAliasLine();
}

void SheetView::updateContentLine()
{
    QModelIndex i = ui->cells->currentIndex();

    if (i.isValid()) {
        std::string str;
        if (const auto * cell = sheet->getCell(CellAddress(i.row(), i.column())))
            (void)cell->getStringContent(str);
        ui->cellContent->setText(QString::fromUtf8(str.c_str()));
        ui->cellContent->setEnabled(true);

        // Update completer model; for the time being, we do this by setting the document object of the input line.
        ui->cellContent->setDocumentObject(sheet);
    }
}

void SheetView::updateAliasLine()
{
    QModelIndex i = ui->cells->currentIndex();

    if (i.isValid()) {
        std::string str;
        if (const auto * cell = sheet->getCell(CellAddress(i.row(), i.column())))
            (void)cell->getAlias(str);
        ui->cellAlias->setText(QString::fromUtf8(str.c_str()));
        ui->cellAlias->setEnabled(true);

        // Update completer model; for the time being, we do this by setting the document object of the input line.
        ui->cellAlias->setDocumentObject(sheet);
    }
}

void SheetView::columnResizeFinished()
{
    if (newColumnSizes.size() == 0)
        return;

    blockSignals(true);
    for(auto &v : newColumnSizes)
        sheet->setColumnWidth(v.first,v.second);
    blockSignals(false);
    newColumnSizes.clear();
}

void SheetView::rowResizeFinished()
{
    if (newRowSizes.size() == 0)
        return;

    blockSignals(true);
    for(auto &v : newRowSizes)
        sheet->setRowHeight(v.first,v.second);
    blockSignals(false);
    newRowSizes.clear();
}

void SheetView::modelUpdated(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    const QModelIndex & current = ui->cells->currentIndex();

    if (current < topLeft || bottomRight < current)
        return;

    updateContentLine();
    updateAliasLine();
}

void SheetView::columnResized(int col, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    newColumnSizes[col] = newSize;
}

void SheetView::rowResized(int row, int oldSize, int newSize)
{
    Q_UNUSED(oldSize);
    newRowSizes[row] = newSize;
}

void SheetView::resizeColumn(int col, int newSize)
{
    if (ui->cells->horizontalHeader()->sectionSize(col) != newSize)
        ui->cells->setColumnWidth(col, newSize);
}

void SheetView::resizeRow(int col, int newSize)
{
    if (ui->cells->verticalHeader()->sectionSize(col) != newSize)
        ui->cells->setRowHeight(col, newSize);
}

void SheetView::editingFinishedWithKey(int key, Qt::KeyboardModifiers modifiers)
{
    QModelIndex i = ui->cells->currentIndex();

    if (i.isValid()) {
        ui->cells->finishEditWithMove(key, modifiers);
    }
}

void SheetView::confirmAliasChanged(const QString& text)
{
    bool aliasOkay = true;

    ui->cellAlias->setDocumentObject(sheet);
    if (text.length() != 0 && !sheet->isValidAlias(Base::Tools::toStdString(text))) {
        aliasOkay = false;
    }

    QModelIndex i = ui->cells->currentIndex();
    if (const auto* cell = sheet->getCell(CellAddress(i.row(), i.column()))) {
        if (!aliasOkay) {
            //do not show error message if failure to set new alias is because it is already the same string
            std::string current_alias;
            (void)cell->getAlias(current_alias);
            if (text != QString::fromUtf8(current_alias.c_str())) {
                Base::Console().Error("Unable to set alias: %s\n", Base::Tools::toStdString(text).c_str());
            }
        }
        else {
            std::string address = CellAddress(i.row(), i.column()).toString();
            Gui::cmdAppObjectArgs(sheet, "setAlias('%s', '%s')",
                address, text.toStdString());
            Gui::cmdAppDocument(sheet->getDocument(), "recompute()");
            ui->cells->setFocus();
        }
    }
}


void SheetView::confirmContentChanged(const QString& text)
{
    QModelIndex i = ui->cells->currentIndex();
    ui->cells->model()->setData(i, QVariant(text), Qt::EditRole);
    ui->cells->setFocus();
}

void SheetView::aliasChanged(const QString& text)
{
    // check live the input and highlight if the user input invalid characters

    bool aliasOk = true;
    QPalette palette = ui->cellAlias->palette();

    if (!text.isEmpty() && !sheet->isValidAlias(Base::Tools::toStdString(text)))
        aliasOk = false;

    if (!aliasOk) {
        // change tooltip and make text color red
        ui->cellAlias->setToolTip(QObject::tr("Alias contains invalid characters!"));
        palette.setColor(QPalette::Text, Qt::red);
    }
    else {
        // go back to normal
        ui->cellAlias->setToolTip(
            QObject::tr("Refer to cell by alias, for example\nSpreadsheet.my_alias_name instead of Spreadsheet.B1"));
        palette.setColor(QPalette::Text, Qt::black);
    }
    // apply the text color via the palette
    ui->cellAlias->setPalette(palette);
}

void SheetView::currentChanged ( const QModelIndex & current, const QModelIndex & previous  )
{
    Q_UNUSED(current);
    Q_UNUSED(previous);
    updateContentLine();
    updateAliasLine();
}

void SheetView::updateCell(const App::Property *prop)
{
    try {
        if (prop == &sheet->Label) {
            QString cap = QString::fromUtf8(sheet->Label.getValue());
            setWindowTitle(cap);
        }
        CellAddress address;

        if(!sheet->getCellAddress(prop, address))
            return;

        if (currentIndex().row() == address.row() && currentIndex().column() == address.col() ){
            updateContentLine();
            updateAliasLine();
        }
    }
    catch (...) {
        // Property is not a cell
        return;
    }
}

std::vector<Range> SheetView::selectedRanges() const
{
    return ui->cells->selectedRanges();
}

QModelIndexList SheetView::selectedIndexes() const
{
    return ui->cells->selectionModel()->selectedIndexes();
}

void SpreadsheetGui::SheetView::select(App::CellAddress cell, QItemSelectionModel::SelectionFlags flags)
{
    ui->cells->selectionModel()->select(model->index(cell.row(), cell.col()), flags);
}

void SpreadsheetGui::SheetView::select(App::CellAddress topLeft, App::CellAddress bottomRight, QItemSelectionModel::SelectionFlags flags)
{
    ui->cells->selectionModel()->select(QItemSelection(model->index(topLeft.row(), topLeft.col()), model->index(bottomRight.row(), bottomRight.col())), flags);
}

void SheetView::deleteSelection()
{
    ui->cells->deleteSelection();
}

QModelIndex SheetView::currentIndex() const
{
    return ui->cells->currentIndex();
}

void SpreadsheetGui::SheetView::setCurrentIndex(App::CellAddress cell) const
{
    ui->cells->setCurrentIndex(model->index(cell.row(), cell.col()));
}

PyObject *SheetView::getPyObject()
{
    if (!pythonObject)
        pythonObject = new SheetViewPy(this);

    Py_INCREF(pythonObject);
    return pythonObject;
}

void SheetView::deleteSelf()
{
    Gui::MDIView::deleteSelf();
}

// ----------------------------------------------------------

void SheetViewPy::init_type()
{
    behaviors().name("SheetViewPy");
    behaviors().doc("Python binding class for the Sheet view class");
    // you must have overwritten the virtual functions
    behaviors().supportRepr();
    behaviors().supportGetattr();
    behaviors().supportSetattr();

    add_varargs_method("getSheet", &SheetViewPy::getSheet, "getSheet()");
    behaviors().readyType();
}

SheetViewPy::SheetViewPy(SheetView *mdi)
  : base(mdi)
{
}

SheetViewPy::~SheetViewPy()
{
}

Py::Object SheetViewPy::repr()
{
    std::ostringstream s_out;
    if (!getSheetViewPtr())
        throw Py::RuntimeError("Cannot print representation of deleted object");
    s_out << "SheetView";
    return Py::String(s_out.str());
}

// Since with PyCXX it's not possible to make a sub-class of MDIViewPy
// a trick is to use MDIViewPy as class member and override getattr() to
// join the attributes of both classes. This way all methods of MDIViewPy
// appear for SheetViewPy, too.
Py::Object SheetViewPy::getattr(const char * attr)
{
    if (!getSheetViewPtr())
        throw Py::RuntimeError("Cannot print representation of deleted object");
    std::string name( attr );
    if (name == "__dict__" || name == "__class__") {
        Py::Dict dict_self(BaseType::getattr("__dict__"));
        Py::Dict dict_base(base.getattr("__dict__"));
        for (auto it : dict_base) {
            dict_self.setItem(it.first, it.second);
        }
        return dict_self;
    }

    try {
        return BaseType::getattr(attr);
    }
    catch (Py::AttributeError& e) {
        e.clear();
        return base.getattr(attr);
    }
}

SheetView* SheetViewPy::getSheetViewPtr()
{
    return qobject_cast<SheetView*>(base.getMDIViewPtr());
}

Py::Object SheetViewPy::getSheet(const Py::Tuple& args)
{
    if (!PyArg_ParseTuple(args.ptr(), ""))
        throw Py::Exception();
    return Py::asObject(new Spreadsheet::SheetPy(getSheetViewPtr()->getSheet()));
}

#include "moc_SpreadsheetView.cpp"
