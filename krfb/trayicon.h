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

#include "configuration.h"

#include <qwidget.h>
#include <kpixmap.h>
#include <kaction.h>
#include <ksystemtray.h>
#include <kpassivepopup.h>

class KDialog;

class KPassivePopup2 : public KPassivePopup {
   	Q_OBJECT
public: 
        KPassivePopup2(QWidget *parent);
        static KPassivePopup2 *message( const QString &caption, const QString &text,
					const QPixmap &icon,
					QWidget *parent);

signals:
	void closed();

protected:
        /**
         * Reimplemented to detect close events.
         */
        virtual void closeEvent( QCloseEvent *e );
};

/**
  * Implements the trayicon. 
  * @author Tim Jansen
  */

class TrayIcon : public KSystemTray {
   	Q_OBJECT
public: 
	TrayIcon(KDialog*, Configuration*);
	~TrayIcon();

signals:
	void showManageInvitations();
	void diconnectedMessageDisplayed();

public slots:
        void prepareQuit();
        void showConnectedMessage();
        void showDisconnectedMessage();

private:
  	KPixmap trayIconOpen;
  	KPixmap trayIconClosed;
	KDialog* aboutDialog;
	KActionCollection actionCollection;
  	KAction* manageInvitationsAction;
  	KAction* aboutAction;
	bool quitting;

private slots:
	void showAbout();
};

#endif
