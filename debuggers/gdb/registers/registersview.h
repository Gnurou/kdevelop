/*
 * Displays registers.
 * Copyright 2013  Vlas Puhov <vlas.puhov@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef REGISTERSVIEW_H
#define REGISTERSVIEW_H

#include "ui_registersview.h"

#include <KConfigGroup>

namespace GDBDebugger {
struct Register;
struct RegistersGroup;
class IRegisterController;
}

class QMenu;

namespace GDBDebugger {

/** @brief Class for displaying registers content */
class RegistersView : public QWidget, private Ui::registersView {
     Q_OBJECT

public:
     RegistersView ( QWidget* p = 0 );

     void setController ( IRegisterController* controller );

public slots:

     ///Updates registers in table if there is association between a table and the @p group.
     void registersInGroupChanged ( const QString& group );

protected:
     ///Allows to choose tables/register formates.
     virtual void contextMenuEvent ( QContextMenuEvent *e );

private:

     enum RegisterColumns {RegisterName, RegisterValue};

     ///Convenient representation of table.
     struct Table {
          Table() {
               tableWidget = 0, name = 0;
          }
          Table ( QTableWidget* _tableWidget, QLabel* _name ) {
               tableWidget = _tableWidget;
               name = _name;
          }
          bool isNull() {
               return !tableWidget;
          }
          QTableWidget* tableWidget;
          QLabel* name;
     };

     ///Association between @p table and @p registers group.
     struct TableRegistersAssociation {
          TableRegistersAssociation() {}
          TableRegistersAssociation ( Table _table, QString _registersGroup ) {
               table = _table;
               registersGroup = _registersGroup;
          }
          RegistersView::Table table;
          QString registersGroup;
     };

     /** @brief Manages associations between tables and register groups.*/
     class TablesManager {
     public:
          TablesManager ( RegistersView* parent );
          ~TablesManager() {
               save();
          }

          void save();
          void load();
          ///Returns the table associated with the @p group, empty table if there is no association.
          Table getTableForGroup ( const QString& group ) const;
          ///Creates association between @p group and table. Returns the table associated with the group, empty table if there is no more empty tables, or already exists association for this group
          Table createTableForGroup ( const QString& group );
          ///Removes association between @p group and table if any.
          bool removeAssociation ( const QString& group );

          ///Adds @p table to the list of available tables
          void addTable ( Table table );

          ///Return names of all associated groups,
          const QStringList getAllGroups() const;

          ///@return: number of empty tables.
          int numOfFreeTables() const;

          void clearAllAssociations();

     private:
          RegistersView* m_parent;
          ///Associations.
          QVector<TableRegistersAssociation> m_tableRegistersAssociation;
          KConfigGroup m_config;
     };

     ///Updates/inserts values from @p registersGroup in table @p tableName.
     void updateRegistersInTable ( Table tableName, const RegistersGroup& registersGroup );

private slots:
     ///Called whenever @p item in any table has changed.
     void registerChangedInternally ( QTableWidgetItem* item );
     ///If item is a flag then send updated value, otherwise do nothing.
     void flagChangedInternally ( QTableWidgetItem* item );
     ///Changes visible tables or register formates.
     void menuTriggered ( QAction* );

signals:
     ///Emitted whenever register @p reg in a table has changed.
     void registerChanged ( const Register& reg );

private:
     void addItemToFormatSubmenu ( QMenu* m, QString name, int format );
private:
     QMenu* m_menu;
     IRegisterController* m_registerController;
     TablesManager m_tablesManager;
     int m_registersFormat;

     friend class TablesManager;
};

}
#endif
