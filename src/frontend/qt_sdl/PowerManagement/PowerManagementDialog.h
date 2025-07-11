/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef POWERMANAGEMENTDIALOG_H
#define POWERMANAGEMENTDIALOG_H

#include <QDialog>
#include <QAbstractButton>

#include "types.h"

namespace Ui { class PowerManagementDialog; }
class PowerManagementDialog;
class EmuInstance;

class PowerManagementDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PowerManagementDialog(QWidget* parent);
    ~PowerManagementDialog();

    static PowerManagementDialog* currentDlg;
    static PowerManagementDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new PowerManagementDialog(parent);
        currentDlg->open();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

private slots:
    void done(int r);

    void on_rbDSBatteryLow_clicked();
    void on_rbDSBatteryOkay_clicked();

    void on_cbDSiBatteryCharging_toggled();
    void on_sliderDSiBatteryLevel_valueChanged(int value);

private:
    Ui::PowerManagementDialog* ui;
    EmuInstance* emuInstance;

    bool inited;
    bool oldDSBatteryLevel;
    melonDS::u8 oldDSiBatteryLevel;
    bool oldDSiBatteryCharging;

    void updateDSBatteryLevelControls();
};

#endif // POWERMANAGEMENTDIALOG_H

