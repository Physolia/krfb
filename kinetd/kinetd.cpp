
/***************************************************************************
                                kinetd.cpp
                              --------------
    begin                : Mon Feb 11 2002
    copyright            : (C) 2002 by Tim Jansen
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

#include "kinetd.h"
#include "kinetd.moc"
#include "kinetaddr.h"
#include "kuser.h"
#include <qregexp.h>
#include <kservicetype.h>
#include <kdebug.h>
#include <kstandarddirs.h>
#include <kconfig.h>
#include <knotifyclient.h>
#include <ksockaddr.h>
#include <kextsock.h>
#include <klocale.h>

PortListener::PortListener(KService::Ptr s, 
			   KConfig *config,
			   KServiceRegistry *srvreg) :
	m_port(-1),
	m_serviceRegistered(false),
	m_socket(0),
	m_config(config),
	m_srvreg(srvreg)
{
	loadConfig(s);

	if (m_valid && m_enabled)
		acquirePort();
}

bool PortListener::acquirePort() {
	if (m_socket) {
		if ((m_port >= m_portBase) &&
		    (m_port < (m_portBase + m_autoPortRange)))
			return true;
		else
			delete m_socket;
	}
	m_port = m_portBase;
	m_socket = new KServerSocket(m_port, false);
	while (!m_socket->bindAndListen()) {
		m_port++;
		if (m_port >= (m_portBase+m_autoPortRange)) {
			kdDebug() << "Kinetd cannot load service "<<m_serviceName
				  <<": unable to get port" << endl;
			m_port = -1;
			delete m_socket;
			m_socket = 0;
			return false;
		}
		delete m_socket;
		m_socket = new KServerSocket(m_port, false);
	}
	connect(m_socket, SIGNAL(accepted(KSocket*)),
		SLOT(accepted(KSocket*)));

	bool s = m_registerService;
	setServiceRegistrationEnabledInternal(false);
	setServiceRegistrationEnabledInternal(s); 
	return true;
}

void PortListener::freePort() {
	m_port = -1;
	if (m_socket)
		delete m_socket;
	m_socket = 0;
	setServiceRegistrationEnabledInternal(m_registerService); 
}

void PortListener::loadConfig(KService::Ptr s) {
	m_valid = true;
	m_autoPortRange = 0;
	m_enabled = true;
	m_argument = QString::null;
	m_multiInstance = false;

	QVariant vid, vport, vautoport, venabled, vargument, vmultiInstance, vurl, vsattributes;

	m_execPath = s->exec().utf8();
	vid = s->property("X-KDE-KINETD-id");
	vport = s->property("X-KDE-KINETD-port");
	vautoport = s->property("X-KDE-KINETD-autoPortRange");
	venabled = s->property("X-KDE-KINETD-enabled");
	vargument = s->property("X-KDE-KINETD-argument");
	vmultiInstance = s->property("X-KDE-KINETD-multiInstance");
	vurl = s->property("X-KDE-KINETD-serviceURL");
	vsattributes = s->property("X-KDE-KINETD-serviceAttributes");

	if (!vid.isValid()) {
		kdDebug() << "Kinetd cannot load service "<<m_serviceName
			  <<": no id set" << endl;
		m_valid = false;
		return;
	}

	if (!vport.isValid()) {
		kdDebug() << "Kinetd cannot load service "<<m_serviceName
			  <<": invalid port" << endl;
		m_valid = false;
		return;
	}

	m_serviceName = vid.toString();
	m_portBase = vport.toInt();
	if (vautoport.isValid())
		m_autoPortRange = vautoport.toInt();
	if (venabled.isValid())
		m_enabled = venabled.toBool();
	if (vargument.isValid())
		m_argument = vargument.toString();
	if (vmultiInstance.isValid())
		m_multiInstance = vmultiInstance.toBool();
	if (vurl.isValid()) {
		m_serviceURL = vurl.toString();
		m_registerService = true;
	}
	else {
		m_serviceURL = QString::null;
		m_registerService = false;
	}
	if (vsattributes.isValid()) {
		m_serviceAttributes = vsattributes.toString();
	}
	else
		m_serviceAttributes = "";

	m_defaultPortBase = m_portBase;
	m_defaultAutoPortRange = m_autoPortRange;

	m_config->setGroup("ListenerConfig");
	m_enabled = m_config->readBoolEntry("enabled_" + m_serviceName, 
					    m_enabled);
	m_portBase = m_config->readNumEntry("port_base_" + m_serviceName, 
					    m_portBase);
	m_autoPortRange = m_config->readNumEntry("auto_port_range_" + m_serviceName, 
						 m_autoPortRange);
	QDateTime nullTime;
	m_expirationTime = m_config->readDateTimeEntry("enabled_expiration_"+m_serviceName,
						     &nullTime);
	if ((!m_expirationTime.isNull()) && (m_expirationTime < QDateTime::currentDateTime()))
		m_enabled = false;
	m_registerService = m_config->readBoolEntry("enabled_srvreg_"+m_serviceName,
						     m_registerService);
}

void PortListener::accepted(KSocket *sock) {
	QString host, port;
	KSocketAddress *ksa = KExtendedSocket::peerAddress(sock->socket());
	KExtendedSocket::resolve(ksa, host, port);
	KNotifyClient::event("IncomingConnection",
		i18n("connection from %1").arg(host));
	delete ksa;

	if ((!m_enabled) ||
	   ((!m_multiInstance) && m_process.isRunning())) {
		delete sock;
		return;
	}

	m_process.clearArguments();
	m_process << m_execPath << m_argument << QString::number(sock->socket());
	if (!m_process.start(KProcess::DontCare)) {
		KNotifyClient::event("ProcessFailed",
			i18n("Call \"%1 %2 %3\" failed").arg(m_execPath)
				.arg(m_argument)
				.arg(sock->socket()));
	}

	delete sock;
}

bool PortListener::isValid() {
	return m_valid;
}

bool PortListener::isEnabled() {
	return m_enabled;
}

int PortListener::port() {
	return m_port;
}

QString PortListener::processServiceTemplate(const QString &a) {
	KInetAddress *kia = KInetAddress::getLocalAddress();
	QString hostName = kia->nodeName();
	delete kia;
	KUser u;
	QString x = a; // replace does not work in const QString. Why??
	return x.replace(QRegExp("%h"), KServiceRegistry::encodeAttributeValue(hostName))
		.replace(QRegExp("%p"), QString::number(m_port))
		.replace(QRegExp("%u"), KServiceRegistry::encodeAttributeValue(u.loginName()))
		.replace(QRegExp("%f"), KServiceRegistry::encodeAttributeValue(u.fullName()));
}

bool PortListener::setPort(int port, int autoPortRange) {
	if ((port == m_portBase) && (autoPortRange == m_autoPortRange))
		return (m_port != -1);

	m_config->setGroup("ListenerConfig");
	if (port > 0) {
		m_portBase = port;
		m_autoPortRange = autoPortRange;

		m_config->writeEntry("port_base_" + m_serviceName, m_portBase);
		m_config->writeEntry("auto_port_range_"+m_serviceName, m_autoPortRange);
	}
	else {
		m_portBase = m_defaultPortBase;
		m_autoPortRange = m_defaultAutoPortRange;		

		m_config->deleteEntry("port_base_" + m_serviceName);
		m_config->deleteEntry("auto_port_range_"+m_serviceName);
	}

	m_config->sync();
	if (m_enabled)
		return acquirePort();
	else
		return false;
}

void PortListener::setEnabled(bool e) {
	setEnabledInternal(e, QDateTime());
}

void PortListener::setEnabledInternal(bool e, const QDateTime &ex) {
	m_config->setGroup("ListenerConfig");
	m_config->writeEntry("enabled_" + m_serviceName, e);
	m_config->writeEntry("enabled_expiration_"+m_serviceName, ex);
	m_config->sync();

	m_expirationTime = ex;

	if (e) {
		if (m_port < 0)
			acquirePort();
		m_enabled = m_port >= 0;
	}
	else {
		freePort();
		m_enabled = false;
	}
}

void PortListener::setEnabled(const QDateTime &ex) {
	setEnabledInternal(true, ex);
}

bool PortListener::isServiceRegistrationEnabled() {
	return m_registerService;
}

void PortListener::setServiceRegistrationEnabled(bool e) {
	setServiceRegistrationEnabledInternal(e);
	m_config->setGroup("ListenerConfig");
	m_config->writeEntry("enable_srvreg_" + m_serviceName, e);
	m_config->sync();
}

void PortListener::setServiceRegistrationEnabledInternal(bool e) {
	m_registerService = e;

	if ((!m_srvreg) || m_serviceURL.isNull())
		return;
	if (m_serviceRegistered == (m_enabled && e)) 
		return;

        if (m_enabled && e) {
		m_registeredServiceURL = processServiceTemplate(m_serviceURL);
		m_serviceRegistered = m_srvreg->registerService(
			m_registeredServiceURL, 
			processServiceTemplate(m_serviceAttributes));
		if (!m_serviceRegistered) 
                      kdDebug(7021) << "Failure registering SLP service (no slpd running?)"<< endl;
	} else {
		m_srvreg->unregisterService(m_registeredServiceURL);
		m_serviceRegistered = false;
	}
}

QDateTime PortListener::expiration() {
	return m_expirationTime;
}

QString PortListener::name() {
	return m_serviceName;
}

PortListener::~PortListener() {
	setServiceRegistrationEnabledInternal(false);
	if (m_socket)
		delete m_socket;
	if (m_config)
		delete m_config;
}


KInetD::KInetD(QCString &n) :
	KDEDModule(n)
{
	m_config = new KConfig("kinetdrc");
	m_srvreg = new KServiceRegistry();
	if (!m_srvreg->available()) {
		kdDebug(7021) << "SLP not available"<< endl;
		delete m_srvreg;
		m_srvreg = 0;
	}
	m_portListeners.setAutoDelete(true);
	connect(&m_expirationTimer, SIGNAL(timeout()), SLOT(setExpirationTimer()));
	connect(&m_portRetryTimer, SIGNAL(timeout()), SLOT(portRetryTimer()));
	loadServiceList();
}

void KInetD::loadServiceList()
{
	m_portListeners.clear();


	KService::List kinetdModules =
		KServiceType::offers("KInetDModule");
	for(KService::List::ConstIterator it = kinetdModules.begin();
		it != kinetdModules.end();
		it++) {
		KService::Ptr s = *it;
		PortListener *pl = new PortListener(s, m_config, m_srvreg);
		if (pl->isValid())
			m_portListeners.append(pl);
	}

	setExpirationTimer();
	setPortRetryTimer(true);
}

void KInetD::setExpirationTimer() {
	QDateTime nextEx = getNextExpirationTime(); // disables expired portlistener!
	if (!nextEx.isNull())
		m_expirationTimer.start(QDateTime::currentDateTime().secsTo(nextEx)*1000 + 30000,
			false);
	else
		m_expirationTimer.stop();
}

void KInetD::portRetryTimer() {
	setPortRetryTimer(true); 
}

void KInetD::setPortRetryTimer(bool retry) {
	int unmappedPorts = 0;

	PortListener *pl = m_portListeners.first();
	while (pl) {
		if (pl->isEnabled() && (pl->port() == -1)) 
			if (retry) {
				if (!pl->acquirePort())
					unmappedPorts++;
			}
			else if (pl->port() == -1)
				unmappedPorts++;
		pl = m_portListeners.next();
	}

	if (unmappedPorts > 0)
		m_portRetryTimer.start(30000, false);
	else
		m_portRetryTimer.stop();
}

PortListener *KInetD::getListenerByName(QString name)
{
	PortListener *pl = m_portListeners.first();
	while (pl) {
		if (pl->name() == name)
			return pl;
		pl = m_portListeners.next();
	}
	return pl;
}

// gets next expiration timer, SIDEEFFECT: disables expired portlisteners while doing this
QDateTime KInetD::getNextExpirationTime()
{
	PortListener *pl = m_portListeners.first();
	QDateTime d;
	while (pl) {
		QDateTime d2 = pl->expiration();
		if (!d2.isNull()) {
			if (d2 < QDateTime::currentDateTime())
				pl->setEnabled(false);
			else if (d.isNull() || (d2 < d))
				d = d2;
		}
		pl = m_portListeners.next();
	}
	return d;
}

QStringList KInetD::services()
{
	QStringList list;
	PortListener *pl = m_portListeners.first();
	while (pl) {
		list.append(pl->name());
		pl = m_portListeners.next();
	}
	return list;
}

bool KInetD::isEnabled(QString service)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return false;

	return pl->isEnabled();
}

int KInetD::port(QString service)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return -1;

	return pl->port();
}

bool KInetD::setPort(QString service, int port, int autoPortRange)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return false;

	bool s = pl->setPort(port, autoPortRange);
	setPortRetryTimer(false);
	return s;
}

bool KInetD::isInstalled(QString service)
{
	PortListener *pl = getListenerByName(service);
	return (pl != 0);
}

void KInetD::setEnabled(QString service, bool enable)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return;

	pl->setEnabled(enable);
	setExpirationTimer();
}

void KInetD::setEnabled(QString service, QDateTime expiration)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return;

	pl->setEnabled(expiration);
	setExpirationTimer();
}

void KInetD::setServiceRegistrationEnabled(QString service, bool enable)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return;

	pl->setServiceRegistrationEnabled(enable);
}

bool KInetD::isServiceRegistrationEnabled(QString service)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return false;

	return pl->isServiceRegistrationEnabled();
}

KInetD::~KInetD() {
	m_portListeners.clear();
	delete m_config;
	if (m_srvreg)
		delete m_srvreg;
}

extern "C" {
	KDEDModule *create_kinetd(QCString &name)
	{
		return new KInetD(name);
	}
}




