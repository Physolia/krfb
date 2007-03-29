/***************************************************************************
                          trayicon.h  -  description
                             -------------------
    begin                : Tue Dec 11 2001
    copyright            : (C) 2001-2002 by Tim Jansen
    email                : tim@tjansen.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef TRAYICON_H
#define TRAYICON_H

#include <ksystemtrayicon.h>
#include <kpassivepopup.h>
#include <KActionCollection>
#include <KToggleAction>

class KDialog;

/**
  * Implements the trayicon.
  * @author Tim Jansen
  */

class TrayIcon : public KSystemTrayIcon {
   	Q_OBJECT
public:
	TrayIcon(KDialog*);
	~TrayIcon();

signals:

	void diconnectedMessageDisplayed();
	void enableDesktopControl(bool);
    void quitApp();

public Q_SLOTS:
    void prepareQuit();
    void showConnectedMessage(QString host);
    void showDisconnectedMessage();
    void setDesktopControlSetting(bool);
    void showManageInvitations();
    void showAbout();

protected:
	void activated(QSystemTrayIcon::ActivationReason reason);

private:

  	QPixmap trayIconOpen;
  	QPixmap trayIconClosed;
	KDialog* aboutDialog;
	KActionCollection actionCollection;
  	KAction* manageInvitationsAction;
  	KAction* aboutAction;
	KToggleAction* enableControlAction;
	bool quitting;

};

#endif
