/***************************************************************************
                              rfbcontroller.cpp
                             -------------------
    begin                : Sun Dec 9 2001
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

/*
 * Contains keyboard & pointer handling from libvncserver's x11vnc.c
 */

#include "rfbcontroller.h"

#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

#include <kapplication.h>
#include <knotifyclient.h>
#include <kdebug.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <kextsock.h>
#include <qstring.h>
#include <qcursor.h>
#include <qwindowdefs.h>
#include <qtimer.h>
#include <qcheckbox.h>
#include <qpushbutton.h>
#include <qglobal.h>
#include <qlabel.h>
#include <qmutex.h>

#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#ifndef ASSERT
#define ASSERT(x) Q_ASSERT(x)
#endif

#define IDLE_PAUSE (1000/50)

static XTestDisabler disabler;

// only one controller exists, so we can do this workaround for functions:
static RFBController *self;

class AppLocker
{
public:
	AppLocker() {
		KApplication::kApplication()->lock();
	}

	~AppLocker() {
		KApplication::kApplication()->unlock();
	}
};

static enum rfbNewClientAction newClientHook(struct _rfbClientRec *cl)
{
	AppLocker a;
	return self->handleNewClient(cl);
}

static Bool passwordCheck(rfbClientPtr cl,
			  const char* encryptedPassword,
			  int len)
{
	AppLocker a;
	return self->handleCheckPassword(cl, encryptedPassword, len);
}

static void keyboardHook(Bool down, KeySym keySym, rfbClientPtr)
{
	self->handleKeyEvent(down ? true : false, keySym);
}

static void pointerHook(int bm, int x, int y, rfbClientPtr)
{
	self->handlePointerEvent(bm, x, y);
}

static void clientGoneHook(rfbClientPtr)
{
	self->handleClientGone();
}



void ConnectionDialog::closeEvent(QCloseEvent *)
{
	emit closed();
}

Display *KeyboardEvent::dpy;
char KeyboardEvent::modifiers[0x100];
KeyCode KeyboardEvent::keycodes[0x100];
KeyCode KeyboardEvent::leftShiftCode;
KeyCode KeyboardEvent::rightShiftCode;
KeyCode KeyboardEvent::altGrCode;
const int KeyboardEvent::LEFTSHIFT = 1;
const int KeyboardEvent::RIGHTSHIFT = 2;
const int KeyboardEvent::ALTGR = 4;
char KeyboardEvent::ModifierState;

KeyboardEvent::KeyboardEvent(bool d, KeySym k) :
	down(d),
	keySym(k) {
}

void KeyboardEvent::initKeycodes() {
	KeySym key,*keymap;
	int i,j,minkey,maxkey,syms_per_keycode;

	dpy = qt_xdisplay();
	
	memset(modifiers,-1,sizeof(modifiers));
	
	XDisplayKeycodes(dpy,&minkey,&maxkey);
	ASSERT(minkey >= 8);
	ASSERT(maxkey < 256);
	keymap = XGetKeyboardMapping(dpy, minkey,
				     (maxkey - minkey + 1),
				     &syms_per_keycode);
	ASSERT(keymap);

	for (i = minkey; i <= maxkey; i++)
		for (j=0; j<syms_per_keycode; j++) {
			key = keymap[(i-minkey)*syms_per_keycode+j];
			if (key>=' ' && key<0x100 && i==XKeysymToKeycode(dpy,key)) {
				keycodes[key]=i;
				modifiers[key]=j;
			}
		}
	
	leftShiftCode = XKeysymToKeycode(dpy, XK_Shift_L);
	rightShiftCode = XKeysymToKeycode(dpy, XK_Shift_R);
	altGrCode = XKeysymToKeycode(dpy, XK_Mode_switch);
	
	XFree ((char *)keymap);
}

/* this function adjusts the modifiers according to mod (as from modifiers) and ModifierState */
void KeyboardEvent::tweakModifiers(char mod, bool down) {
	
	bool isShift = ModifierState & (LEFTSHIFT|RIGHTSHIFT);
	if(mod < 0) 
		return;
	
	if(isShift && mod != 1) {
		if(ModifierState & LEFTSHIFT)
			XTestFakeKeyEvent(dpy, leftShiftCode,
					  !down, CurrentTime);
		if(ModifierState & RIGHTSHIFT)
				XTestFakeKeyEvent(dpy, rightShiftCode,
						  !down, CurrentTime);
	}
	
	if(!isShift && mod==1)
		XTestFakeKeyEvent(dpy, leftShiftCode,
				  down, CurrentTime);
	
	if((ModifierState&ALTGR) && mod != 2)
		XTestFakeKeyEvent(dpy, altGrCode,
				  !down, CurrentTime);
	if(!(ModifierState&ALTGR) && mod==2)
		XTestFakeKeyEvent(dpy, altGrCode,
				  down, CurrentTime);
}

void KeyboardEvent::exec() {
#define ADJUSTMOD(sym,state) \
  if(keySym==sym) { if(down) ModifierState|=state; else ModifierState&=~state; }
	
	ADJUSTMOD(XK_Shift_L,LEFTSHIFT);
	ADJUSTMOD(XK_Shift_R,RIGHTSHIFT);
	ADJUSTMOD(XK_Mode_switch,ALTGR);
	
	if(keySym>=' ' && keySym<0x100) {
		KeyCode k;
		if (down)
			tweakModifiers(modifiers[keySym],True);
		k = keycodes[keySym];
		if (k != NoSymbol) 
			XTestFakeKeyEvent(dpy, k, down, CurrentTime);
		
		if (down)
			tweakModifiers(modifiers[keySym],False);
	} else {
		KeyCode k = XKeysymToKeycode(dpy, keySym );
		if (k != NoSymbol)
			XTestFakeKeyEvent(dpy, k, down, CurrentTime);
	}
}

bool PointerEvent::initialized = false;
Display *PointerEvent::dpy;
int PointerEvent::buttonMask = 0;

PointerEvent::PointerEvent(int b, int _x, int _y) :
	button_mask(b),
	x(_x),
	y(_y) {
	if (!initialized) {
		initialized = true;
		dpy = qt_xdisplay();
		buttonMask = 0;
	}
}

void PointerEvent::exec() {
	XTestFakeMotionEvent(dpy, 0, x, y, CurrentTime);
	for(int i = 0; i < 5; i++)
		if ((buttonMask&(1<<i))!=(button_mask&(1<<i)))
			XTestFakeButtonEvent(dpy,
					     i+1,
					     (button_mask&(1<<i))?True:False,
					     CurrentTime);

	buttonMask = button_mask;
}

KNotifyEvent::KNotifyEvent(const QString &n, const QString &d) :
	name(n),
	desc(d) {
}

KNotifyEvent::~KNotifyEvent() {
}

void KNotifyEvent::exec() {
	KNotifyClient::event(name, desc);
}

SessionEstablishedEvent::SessionEstablishedEvent(RFBController *c) :
        controller(c)
{ }

void SessionEstablishedEvent::exec() {
        controller->sendSessionEstablished();
}



RFBController::RFBController(Configuration *c) :
	allowRemoteControl(false),
	connectionNum(0),
	configuration(c),
	closePending(false),
	forcedClose(false)
{
	self = this;
	connect(dialog.acceptConnectionButton, SIGNAL(clicked()),
		SLOT(dialogAccepted()));
	connect(dialog.refuseConnectionButton, SIGNAL(clicked()),
		SLOT(dialogRefused()));
	connect(&dialog, SIGNAL(closed()), SLOT(dialogRefused()));
	connect(&idleTimer, SIGNAL(timeout()), SLOT(idleSlot()));

	asyncQueue.setAutoDelete(true);

	KeyboardEvent::initKeycodes();
}

RFBController::~RFBController()
{
	stopServer();
}



void RFBController::startServer(int inetdFd, bool xtestGrab)
{
	framebufferImage = XGetImage(qt_xdisplay(),
				     QApplication::desktop()->winId(),
				     0,
				     0,
				     QApplication::desktop()->width(),
				     QApplication::desktop()->height(),
				     AllPlanes,
				     ZPixmap);

	int w = framebufferImage->width;
	int h = framebufferImage->height;
	char *fb = framebufferImage->data;

	rfbLogEnable(0);
	server = rfbGetScreen(0, 0, w, h,
			      framebufferImage->bits_per_pixel,
			      8,
			      framebufferImage->bits_per_pixel/8);

	server->paddedWidthInBytes = framebufferImage->bytes_per_line;

	server->rfbServerFormat.bitsPerPixel = framebufferImage->bits_per_pixel;
	server->rfbServerFormat.depth = framebufferImage->depth;
	//rfbEndianTest = framebufferImage->bitmap_bit_order != MSBFirst;
	server->rfbServerFormat.trueColour = (CARD8) TRUE;

	if ( server->rfbServerFormat.bitsPerPixel == 8 ) {
		server->rfbServerFormat.redShift = 0;
		server->rfbServerFormat.greenShift = 2;
		server->rfbServerFormat.blueShift = 5;
		server->rfbServerFormat.redMax   = 3;
		server->rfbServerFormat.greenMax = 7;
		server->rfbServerFormat.blueMax  = 3;
	} else {
		server->rfbServerFormat.redShift = 0;
		if ( framebufferImage->red_mask )
			while ( ! ( framebufferImage->red_mask & (1 << server->rfbServerFormat.redShift) ) )
				server->rfbServerFormat.redShift++;
		server->rfbServerFormat.greenShift = 0;
		if ( framebufferImage->green_mask )
			while ( ! ( framebufferImage->green_mask & (1 << server->rfbServerFormat.greenShift) ) )
				server->rfbServerFormat.greenShift++;
		server->rfbServerFormat.blueShift = 0;
		if ( framebufferImage->blue_mask )
			while ( ! ( framebufferImage->blue_mask & (1 << server->rfbServerFormat.blueShift) ) )
				server->rfbServerFormat.blueShift++;
		server->rfbServerFormat.redMax   = framebufferImage->red_mask   >> server->rfbServerFormat.redShift;
		server->rfbServerFormat.greenMax = framebufferImage->green_mask >> server->rfbServerFormat.greenShift;
		server->rfbServerFormat.blueMax  = framebufferImage->blue_mask  >> server->rfbServerFormat.blueShift;
	}

	server->frameBuffer = fb;
	server->autoPort = TRUE;
	server->inetdSock = inetdFd;

	server->kbdAddEvent = keyboardHook;
	server->ptrAddEvent = pointerHook;
	server->newClientHook = newClientHook;
	server->passwordCheck = passwordCheck;

	passwordChanged();

	scanner = new XUpdateScanner(qt_xdisplay(),
				     QApplication::desktop()->winId(),
				     (unsigned char*)fb, w, h,
				     server->rfbServerFormat.bitsPerPixel,
				     server->paddedWidthInBytes);

	rfbInitServer(server);
	state = RFB_WAITING;

	if (xtestGrab) {
		disabler.disable = false;
		XTestGrabControl(qt_xdisplay(), true);
	}

	rfbRunEventLoop(server, -1, TRUE);
}

void RFBController::stopServer(bool xtestUngrab)
{
	rfbScreenCleanup(server);
	state = RFB_STOPPED;
	delete scanner;

	XDestroyImage(framebufferImage);

	if (xtestUngrab) {
		disabler.disable = true;
		QTimer::singleShot(0, &disabler, SLOT(exec()));
	}
}

void RFBController::connectionAccepted(bool aRC)
{
	if (state != RFB_CONNECTING)
		return;

	allowRemoteControl = aRC;
	connectionNum++;
	idleTimer.start(IDLE_PAUSE);

	server->rfbClientHead->clientGoneHook = clientGoneHook;
	state = RFB_CONNECTED;
	if (!server->rfbAuthPasswdData)
	        emit sessionEstablished();
}

void RFBController::acceptConnection(bool aRC)
{
	KNotifyClient::event("UserAcceptsConnection",
			     i18n("User accepts connection from %1")
			     .arg(remoteIp));

	if (state != RFB_CONNECTING)
		return;

	connectionAccepted(aRC);
	rfbStartOnHoldClient(server->rfbClientHead);
}

void RFBController::refuseConnection()
{
	KNotifyClient::event("UserRefusesConnection",
			     i18n("User refuses connection from %1")
			     .arg(remoteIp));

	if (state != RFB_CONNECTING)
		return;
	rfbRefuseOnHoldClient(server->rfbClientHead);
	state = RFB_WAITING;
}

// checks async events, returns true if client disconnected
bool RFBController::checkAsyncEvents()
{
	bool closed = false;
	asyncMutex.lock();
	VNCEvent *e;
	for (e = asyncQueue.first(); e; e = asyncQueue.next())
		e->exec();
	asyncQueue.clear();
	if (closePending) {
		connectionClosed();
		closed = true;
		closePending = false;
	}
	asyncMutex.unlock();
	return closed;
}

void RFBController::connectionClosed()
{
	KNotifyClient::event("ConnectionClosed",
			     i18n("Closed connection: %1.")
			     .arg(remoteIp));

	idleTimer.stop();
	connectionNum--;
	state = RFB_WAITING;
	if (forcedClose)
	        emit quitApp();
	else
	        emit sessionFinished();
}

void RFBController::closeConnection()
{
        forcedClose = true;
	if (state == RFB_CONNECTED) {
		if (!checkAsyncEvents()) {
			asyncMutex.lock();
			if (!closePending)
				rfbCloseClient(server->rfbClientHead);
			asyncMutex.unlock();
		}
	}
	else if (state == RFB_CONNECTING)
		refuseConnection();
}

void RFBController::idleSlot()
{
	if (state != RFB_CONNECTED)
		return;
	if (checkAsyncEvents())
		return;

	rfbUndrawCursor(server);

	QPtrList<Hint> v;
	v.setAutoDelete(true);
	scanner->searchUpdates(v);

	Hint *h;

	for (h = v.first(); h != 0; h = v.next())
		rfbMarkRectAsModified(server, h->left(),
				      h->top(),
				      h->right(),
				      h->bottom());

	QPoint p = QCursor::pos();
	asyncMutex.lock();
	if (!closePending)
		defaultPtrAddEvent(0, p.x(),p.y(), server->rfbClientHead);
	asyncMutex.unlock();

	checkAsyncEvents();
}

void RFBController::dialogAccepted()
{
	dialog.hide();
	acceptConnection(dialog.allowRemoteControlCB->isChecked());
}

void RFBController::dialogRefused()
{
	refuseConnection();
	dialog.hide();
	emit sessionRefused();
}

bool checkPassword(const QString &p,
	unsigned char *ochallenge,
	const char *response,
	int len) {

	if ((len == 0) && (p.length() == 0))
		return true;

	char passwd[MAXPWLEN];
	unsigned char challenge[CHALLENGESIZE];

	memcpy(challenge, ochallenge, CHALLENGESIZE);
	bzero(passwd, MAXPWLEN);
	if (!p.isNull())
		strncpy(passwd, p.latin1(),
			(MAXPWLEN <= p.length()) ? MAXPWLEN : p.length());

	vncEncryptBytes(challenge, passwd);
	return memcmp(challenge, response, len) == 0;
}

bool RFBController::handleCheckPassword(rfbClientPtr cl,
					const char *response,
					int len)
{

	bool authd = false;

	if (configuration->allowUninvitedConnections())
		authd = checkPassword(configuration->password(),
			cl->authChallenge, response, len);

	if (!authd) {
		QValueList<Invitation>::iterator it =
			configuration->invitations().begin();
		while (it != configuration->invitations().end()) {
			if (checkPassword((*it).password(),
				cl->authChallenge, response, len) &&
				(*it).isValid()) {
				authd = true;
				configuration->removeInvitation(it);
				break;
			}
			it++;
		}
	}

	if (!authd) {
		if (configuration->invitations().size() > 0) {
			sendKNotifyEvent("InvalidPasswordInvitations",
					i18n("Failed login attempt from %1: wrong password")
					.arg(remoteIp));
}
		else
			sendKNotifyEvent("InvalidPassword",
					i18n("Failed login attempt from %1: wrong password")
					.arg(remoteIp));
		return FALSE;
	}

	asyncMutex.lock();
	asyncQueue.append(new SessionEstablishedEvent(this));
	asyncMutex.unlock();
        
	return TRUE;
}

enum rfbNewClientAction RFBController::handleNewClient(rfbClientPtr cl)
{
	int socket = cl->sock;

	QString host, port;
	KSocketAddress *ksa = KExtendedSocket::peerAddress(socket);
	if (ksa) {
		KExtendedSocket::resolve(ksa, host, port);
		delete ksa;
	}

	if ((connectionNum > 0) ||
	    (state != RFB_WAITING)) {
		sendKNotifyEvent("TooManyConnections",
					i18n("Connection refused from %1, already connected.")
					.arg(host));
		return RFB_CLIENT_REFUSE;
	}
	remoteIp = host;
	state = RFB_CONNECTING;

	if ((!configuration->askOnConnect()) &&
	    (configuration->invitations().size() == 0)) {
		sendKNotifyEvent("NewConnectionAutoAccepted",
					i18n("Accepted uninvited connection from %1")
					.arg(remoteIp));

		connectionAccepted(configuration->allowDesktopControl());
		return RFB_CLIENT_ACCEPT;
	}

	sendKNotifyEvent("NewConnectionOnHold",
				i18n("Received connection from %1, on hold (waiting for confirmation)")
				.arg(remoteIp));

	dialog.ipLabel->setText(remoteIp);
	dialog.allowRemoteControlCB->setChecked(configuration->allowDesktopControl());
	dialog.setFixedSize(dialog.sizeHint());
	dialog.show();
	return RFB_CLIENT_ON_HOLD;
}

void RFBController::handleClientGone()
{
	asyncMutex.lock();
	closePending = true;
	asyncMutex.unlock();
}

void RFBController::handleKeyEvent(bool down, KeySym keySym) {
	if (!allowRemoteControl)
		return;

	asyncMutex.lock();
	asyncQueue.append(new KeyboardEvent(down, keySym));
	asyncMutex.unlock();
}

void RFBController::handlePointerEvent(int button_mask, int x, int y) {
	if (!allowRemoteControl)
		return;

	asyncMutex.lock();
	asyncQueue.append(new PointerEvent(button_mask, x, y));
	asyncMutex.unlock();
}

void RFBController::passwordChanged() {
	bool authRequired = (!configuration->allowUninvitedConnections()) ||
		(configuration->password().length() != 0) ||
		(configuration->invitations().count() > 0);

	server->rfbAuthPasswdData = (void*) (authRequired ? 1 : 0);
}

void RFBController::sendKNotifyEvent(const QString &n, const QString &d)
{
	asyncMutex.lock();
	asyncQueue.append(new KNotifyEvent(n, d));
	asyncMutex.unlock();
}

void RFBController::sendSessionEstablished()
{
        emit sessionEstablished();
}

bool RFBController::checkX11Capabilities() {
	int bp1, bp2, majorv, minorv;
	Bool r = XTestQueryExtension(qt_xdisplay(), &bp1, &bp2,
				     &majorv, &minorv);
	if ((!r) || (((majorv*1000)+minorv) < 2002)) {
		KMessageBox::error(0,
		   i18n("Your X11 Server does not support the required XTest extension version 2.2. Sharing your desktop is not possible."),
				   i18n("Desktop Sharing Error"));
		return false;
	}

	r = XShmQueryExtension(qt_xdisplay());
	if (!r) {
		KMessageBox::error(0,
		   i18n("Your X11 Server does not support the required XShm extension. You can only share a local desktop."),
				   i18n("Desktop Sharing Error"));
		return false;
	}
	return true;
}


XTestDisabler::XTestDisabler() :
	disable(false) {
}

void XTestDisabler::exec() {
	if (disable)
		XTestDiscard(qt_xdisplay());
}

#include "rfbcontroller.moc"
