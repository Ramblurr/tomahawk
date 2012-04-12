/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2012, Casey Link <unnamedrambler@gmail.com>
 *
 *   Tomahawk is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Tomahawk is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Tomahawk. If not, see <http://www.gnu.org/licenses/>.
 */

#include "tomahawkcache.h"

#include "tomahawksettings.h"

#include <QDateTime>
#include <QSettings>

using namespace TomahawkUtils;

TomahawkCache*TomahawkCache::s_instance = 0;

TomahawkCache* TomahawkCache::instance()
{
    if ( !s_instance )
        s_instance = new TomahawkCache();

    return s_instance;
}

TomahawkCache::TomahawkCache()
    : QObject ( 0 )
    , m_cacheBaseDir ( TomahawkSettings::instance()->storageCacheLocation() + "/GenericCache/" )
    , m_cacheManifest ( m_cacheBaseDir + "cachemanifest.ini", QSettings::IniFormat )
{
    m_pruneTimer.setInterval ( 300000 );
    m_pruneTimer.setSingleShot ( false );
    connect ( &m_pruneTimer, SIGNAL ( timeout() ), SLOT ( pruneTimerFired() ) );
    m_pruneTimer.start();
}

TomahawkCache::~TomahawkCache()
{

}

void TomahawkCache::pruneTimerFired()
{
    qDebug() << Q_FUNC_INFO << "Pruning tomahawkcache";
    qlonglong currentMSecsSinceEpoch = QDateTime::currentMSecsSinceEpoch();

    QVariantList clients = m_cacheManifest.value ( "clients" ).toList();
    foreach ( const QVariant &client, clients ) {
        const QString client_identifier = client.toString();
        const QString cache_dir = m_cacheBaseDir + client_identifier;

        QSettings cached_settings ( cache_dir, QSettings::IniFormat );
        const QStringList keys = cached_settings.allKeys();
        foreach ( const QString &key, keys ) {
            CacheData data = cached_settings.value ( key ).value<TomahawkUtils::CacheData>();
            if ( data.maxAge < currentMSecsSinceEpoch ) {
                cached_settings.remove ( key );
                tLog() << Q_FUNC_INFO << "Removed stale entry: " << client_identifier << key;
            }
        }
        cached_settings.sync();
        if ( cached_settings.allKeys().size() == 0 )
            removeClient ( client_identifier );
    }
}


QVariant TomahawkCache::getData ( const QString& identifier, const QString& key )
{
    const QString cacheDir = m_cacheBaseDir + identifier;
    QSettings cached_settings ( cacheDir, QSettings::IniFormat );

    if ( cached_settings.contains ( key ) ) {
        CacheData data = cached_settings.value ( key ).value<TomahawkUtils::CacheData>();

        if ( data.maxAge < QDateTime::currentMSecsSinceEpoch() ) {
            cached_settings.remove ( key );
            tLog() << Q_FUNC_INFO << "Removed stale entry: " << identifier << key;
            return QVariant();
        }
        return data.data;

    }
    return QVariant();
}

void TomahawkCache::putData ( const QString& identifier, qint64 maxAge, const QString& key, const QVariant& value )
{
    const QString cacheDir = m_cacheBaseDir + identifier;
    addClient ( identifier );
    QSettings cached_settings ( cacheDir, QSettings::IniFormat );
    cached_settings.setValue ( key, QVariant::fromValue ( CacheData ( maxAge, value ) ) );
}

void TomahawkCache::addClient ( const QString& identifier )
{
    QVariantList clients = m_cacheManifest.value ( "clients" ).toList();
    foreach ( const QVariant &client, clients ) {
        const QString client_identifier = client.toString();
        if ( identifier == client_identifier ) return;
    }

    tLog() << Q_FUNC_INFO << "adding client" << identifier;
    clients.append ( identifier );
    m_cacheManifest.setValue ( "clients", clients );
    m_cacheManifest.sync();
}

void TomahawkCache::removeClient ( const QString& identifier )
{
    QVariantList clients = m_cacheManifest.value ( "clients" ).toList();
    QVariantList::iterator it = clients.begin();
    while ( it != clients.end() ) {
        const QString client_identifier = it->toString();
        if ( identifier == client_identifier ) {
            tLog() << Q_FUNC_INFO << "removing client" << identifier;
            clients.erase ( it );
            break;
        }
        ++it;
    }
    m_cacheManifest.setValue ( "clients", clients );
    m_cacheManifest.sync();
}




