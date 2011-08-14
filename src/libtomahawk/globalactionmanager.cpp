/*
    Copyright (C) 2011  Leo Franchi <lfranchi@kde.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "globalactionmanager.h"

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

#include "artist.h"
#include "album.h"
#include "sourcelist.h"
#include "pipeline.h"
#include "viewmanager.h"
#include "audio/audioengine.h"
#include "database/localcollection.h"
#include "playlist/dynamic/GeneratorInterface.h"
#include "playlist/topbar/topbar.h"
#include "playlist/playlistview.h"

#include "echonest/Playlist.h"

#include "utils/xspfloader.h"
#include "utils/xspfgenerator.h"
#include "utils/logger.h"
#include "utils/tomahawkutils.h"

#include "utils/jspfloader.h"
#include "utils/spotifyparser.h"
#include "utils/shortenedlinkparser.h"
#include "utils/rdioparser.h"

GlobalActionManager* GlobalActionManager::s_instance = 0;

using namespace Tomahawk;

GlobalActionManager*
GlobalActionManager::instance()
{
    if( !s_instance )
        s_instance = new GlobalActionManager;

    return s_instance;
}

GlobalActionManager::GlobalActionManager( QObject* parent )
    : QObject( parent )
{
    m_mimeTypes << "application/tomahawk.query.list" << "application/tomahawk.plentry.list" << "application/tomahawk.result.list" << "text/plain";
}

GlobalActionManager::~GlobalActionManager()
{}

QUrl
GlobalActionManager::openLinkFromQuery( const query_ptr& query ) const
{
    QString title, artist, album;

    if( !query->results().isEmpty() && !query->results().first().isNull() )
    {
        title = query->results().first()->track();
        artist = query->results().first()->artist().isNull() ? QString() : query->results().first()->artist()->name();
        album = query->results().first()->album().isNull() ? QString() : query->results().first()->album()->name();
    } else
    {
        title = query->track();
        artist = query->artist();
        album = query->album();
    }

    return openLink( title, artist, album );
}

QUrl
GlobalActionManager::openLink( const QString& title, const QString& artist, const QString& album ) const
{
    QUrl link( QString( "%1/open/track/" ).arg( hostname() ) );

    if( !title.isEmpty() )
        link.addQueryItem( "title", title );
    if( !artist.isEmpty() )
        link.addQueryItem( "artist", artist );
    if( !album.isEmpty() )
        link.addQueryItem( "album", album );

    return link;
}

QString
GlobalActionManager::copyPlaylistToClipboard( const dynplaylist_ptr& playlist )
{
    QUrl link( QString( "%1/%2/create/" ).arg( hostname() ).arg( playlist->mode() == OnDemand ? "station" : "autoplaylist" ) );

    if( playlist->generator()->type() != "echonest" ) {
        tLog() << "Only echonest generators are supported";
        return QString();
    }

    link.addEncodedQueryItem( "type", "echonest" );
    link.addQueryItem( "title", playlist->title() );

    QList< dyncontrol_ptr > controls = playlist->generator()->controls();
    foreach( const dyncontrol_ptr& c, controls ) {
        if( c->selectedType() == "Artist" ) {
            if( c->match().toInt() == Echonest::DynamicPlaylist::ArtistType )
                link.addQueryItem( "artist_limitto", c->input() );
            else
                link.addQueryItem( "artist", c->input() );
        } else if( c->selectedType() == "Artist Description" ) {
            link.addQueryItem( "description", c->input() );
        } else {
            QString name = c->selectedType().toLower().replace( " ", "_" );
            Echonest::DynamicPlaylist::PlaylistParam p = static_cast< Echonest::DynamicPlaylist::PlaylistParam >( c->match().toInt() );
            // if it is a max, set that too
            if( p == Echonest::DynamicPlaylist::MaxTempo || p == Echonest::DynamicPlaylist::MaxDuration || p == Echonest::DynamicPlaylist::MaxLoudness
               || p == Echonest::DynamicPlaylist::MaxDanceability || p == Echonest::DynamicPlaylist::MaxEnergy || p == Echonest::DynamicPlaylist::ArtistMaxFamiliarity
               || p == Echonest::DynamicPlaylist::ArtistMaxHotttnesss || p == Echonest::DynamicPlaylist::SongMaxHotttnesss || p == Echonest::DynamicPlaylist::ArtistMaxLatitude
               || p == Echonest::DynamicPlaylist::ArtistMaxLongitude )
                name += "_max";

            link.addQueryItem( name, c->input() );
        }
    }

    QClipboard* cb = QApplication::clipboard();
    QByteArray data = link.toEncoded();
    data.replace( "'", "%27" ); // QUrl doesn't encode ', which it doesn't have to. Some apps don't like ' though, and want %27. Both are valid.
    cb->setText( data );

    return link.toString();
}

void
GlobalActionManager::savePlaylistToFile( const playlist_ptr& playlist, const QString& filename )
{
    XSPFGenerator* g = new XSPFGenerator( playlist, this );
    g->setProperty( "filename", filename );

    connect( g, SIGNAL( generated( QByteArray ) ), this, SLOT( xspfCreated( QByteArray ) ) );
}

void
GlobalActionManager::xspfCreated( const QByteArray& xspf )
{
    QString filename = sender()->property( "filename" ).toString();

    QFile f( filename );
    if( !f.open( QIODevice::WriteOnly ) ) {
        qWarning() << "Failed to open file to save XSPF:" << filename;
        return;
    }

    f.write( xspf );
    f.close();

    sender()->deleteLater();
}


void
GlobalActionManager::copyToClipboard( const query_ptr& query ) const
{
    QClipboard* cb = QApplication::clipboard();
    QByteArray data = openLinkFromQuery( query ).toEncoded();
    data.replace( "'", "%27" ); // QUrl doesn't encode ', which it doesn't have to. Some apps don't like ' though, and want %27. Both are valid.
    cb->setText( data );
}


bool
GlobalActionManager::parseTomahawkLink( const QString& url )
{
    if( url.contains( "tomahawk://" ) ) {
        QString cmd = url.mid( 11 );
        cmd.replace( "%2B", "%20" );
        tLog() << "Parsing tomahawk link command" << cmd;

        QString cmdType = cmd.split( "/" ).first();
        QUrl u = QUrl::fromEncoded( cmd.toUtf8() );

        // for backwards compatibility
        if( cmdType == "load" ) {
            if( u.hasQueryItem( "xspf" ) ) {
                QUrl xspf = QUrl::fromUserInput( u.queryItemValue( "xspf" ) );
                XSPFLoader* l = new XSPFLoader( true, this );
                tDebug() << "Loading spiff:" << xspf.toString();
                l->load( xspf );
                connect( l, SIGNAL( ok( Tomahawk::playlist_ptr ) ), ViewManager::instance(), SLOT( show( Tomahawk::playlist_ptr ) ) );

                return true;
            } else if( u.hasQueryItem( "jspf" ) ) {
                QUrl jspf = QUrl::fromUserInput( u.queryItemValue( "jspf" ) );
                JSPFLoader* l = new JSPFLoader( true, this );

                tDebug() << "Loading jspiff:" << jspf.toString();
                l->load( jspf );
                connect( l, SIGNAL( ok( Tomahawk::playlist_ptr ) ), ViewManager::instance(), SLOT( show( Tomahawk::playlist_ptr ) ) );

                return true;
            }
        }

        if( cmdType == "playlist" ) {
            return handlePlaylistCommand( u );
        } else if( cmdType == "collection" ) {
            return handleCollectionCommand( u );
        } else if( cmdType == "queue" ) {
            return handleQueueCommand( u );
        } else if( cmdType == "station" ) {
            return handleStationCommand( u );
        } else if( cmdType == "autoplaylist" ) {
            return handleAutoPlaylistCommand( u );
        } else if( cmdType == "search" ) {
            return handleSearchCommand( u );
        } else if( cmdType == "play" ) {
            return handlePlayCommand( u );
        } else if( cmdType == "bookmark" ) {
            return handlePlayCommand( u );
        } else if( cmdType == "open" ) {
            return handleOpenCommand( u );
        } else {
            tLog() << "Tomahawk link not supported, command not known!" << cmdType << u.path();
            return false;
        }
    } else {
        tLog() << "Not a tomahawk:// link!";
        return false;
    }
}

bool
GlobalActionManager::handlePlaylistCommand( const QUrl& url )
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific playlist command:" << url.toString();
        return false;
    }

    if( parts[ 0 ] == "import" ) {
        if( !url.hasQueryItem( "xspf" ) ) {
            tDebug() << "No xspf to load...";
            return false;
        }
        QUrl xspf = QUrl( url.queryItemValue( "xspf" ) );
        QString title =  url.hasQueryItem( "title" ) ? url.queryItemValue( "title" ) : QString();
        XSPFLoader* l= new XSPFLoader( true, this );
        l->setOverrideTitle( title );
        l->load( xspf );
        connect( l, SIGNAL( ok( Tomahawk::playlist_ptr ) ), ViewManager::instance(), SLOT( show( Tomahawk::playlist_ptr ) ) );

    } else if( parts [ 0 ] == "new" ) {
        if( !url.hasQueryItem( "title" ) ) {
            tLog() << "New playlist command needs a title...";
            return false;
        }
        playlist_ptr pl = Playlist::create( SourceList::instance()->getLocal(), uuid(), url.queryItemValue( "title" ), QString(), QString(), false );
        ViewManager::instance()->show( pl );
    } else if( parts[ 0 ] == "add" ) {
        if( !url.hasQueryItem( "playlistid" ) || !url.hasQueryItem( "title" ) || !url.hasQueryItem( "artist" ) ) {
            tLog() << "Add to playlist command needs playlistid, track, and artist..." << url.toString();
            return false;
        }
        // TODO implement. Let the user select what playlist to add to
        return false;
    }

    return false;
}

bool
GlobalActionManager::handleCollectionCommand( const QUrl& url )
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific collection command:" << url.toString();
        return false;
    }

    if( parts[ 0 ] == "add" ) {
        // TODO implement
    }

    return false;
}

bool
GlobalActionManager::handleOpenCommand(const QUrl& url)
{
    QStringList parts = url.path().split( "/" ).mid( 1 );
    if( parts.isEmpty() ) {
        tLog() << "No specific type to open:" << url.toString();
        return false;
    }
    // TODO user configurable in the UI
    return doQueueAdd( parts, url.queryItems() );
}

void
GlobalActionManager::handleOpenTrack ( const query_ptr& q )
{
    ViewManager::instance()->queue()->model()->append( q );
    ViewManager::instance()->showQueue();

    if( !AudioEngine::instance()->isPlaying() ) {
        connect( q.data(), SIGNAL( resolvingFinished( bool ) ), this, SLOT( waitingForResolved( bool ) ) );
        m_waitingToPlay = q;
    }
}


bool
GlobalActionManager::handleQueueCommand( const QUrl& url )
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific queue command:" << url.toString();
        return false;
    }

    if( parts[ 0 ] == "add" ) {
        doQueueAdd( parts.mid( 1 ), url.queryItems() );
    } else {
        tLog() << "Only queue/add/track is support at the moment, got:" << parts;
        return false;
    }

    return false;
}

bool
GlobalActionManager::doQueueAdd( const QStringList& parts, const QList< QPair< QString, QString > >& queryItems )
{
    if( parts.size() && parts[ 0 ] == "track" ) {

        if( queueSpotify( parts, queryItems ) )
            return true;

        QPair< QString, QString > pair;

        QString title, artist, album, urlStr;
        foreach( pair, queryItems ) {
            if( pair.first == "title" )
                title = pair.second;
            else if( pair.first == "artist" )
                artist = pair.second;
            else if( pair.first == "album" )
                album = pair.second;
            else if( pair.first == "url" )
                urlStr = pair.second;
        }

        if( !title.isEmpty() || !artist.isEmpty() || !album.isEmpty() ) { // an individual; query to add to queue
            query_ptr q = Query::get( artist, title, album, uuid(), false );
            if( !urlStr.isEmpty() )
                q->setResultHint( urlStr );
            Pipeline::instance()->resolve( q, true );

            handleOpenTrack( q );
            return true;

        } else { // a list of urls to add to the queue
            foreach( pair, queryItems ) {
                if( pair.first != "url" )
                    continue;
                QUrl track = QUrl::fromUserInput( pair.second  );
                //FIXME: isLocalFile is Qt 4.8
                if( track.toString().startsWith( "file://" ) ) { // it's local, so we see if it's in the DB and load it if so
                    // TODO
                } else { // give it a web result hint
                    QFileInfo info( track.path() );
                    query_ptr q = Query::get( QString(), info.baseName(), QString(), uuid(), false );
                    q->setResultHint( track.toString() );

                    Pipeline::instance()->resolve( q, true );

                    ViewManager::instance()->queue()->model()->append( q );
                    ViewManager::instance()->showQueue();
                }
                return true;
            }
        }
    }
    return false;
}

bool
GlobalActionManager::queueSpotify( const QStringList& , const QList< QPair< QString, QString > >& queryItems )
{
    QString url;

    QPair< QString, QString > pair;
    foreach( pair, queryItems ) {
        if( pair.first == "spotifyURL" )
            url = pair.second;
        else if( pair.first == "spotifyURI" )
            url = pair.second;
    }

    if( url.isEmpty() )
        return false;

    openSpotifyLink( url );

    return true;
}


bool
GlobalActionManager::handleSearchCommand( const QUrl& url )
{
    // open the super collection and set this as the search filter
    QStringList query;
    if( url.hasQueryItem( "artist" ) )
        query << url.queryItemValue( "artist" );
    if( url.hasQueryItem( "album" ) )
        query << url.queryItemValue( "album" );
    if( url.hasQueryItem( "title" ) )
        query << url.queryItemValue( "title" );
    QString queryStr = query.join( " " );

    if( queryStr.isEmpty() )
        return false;

    ViewManager::instance()->showSuperCollection();
    ViewManager::instance()->topbar()->setFilter( queryStr );
    return true;
}

bool
GlobalActionManager::handleAutoPlaylistCommand( const QUrl& url )
{
    return !loadDynamicPlaylist( url, false ).isNull();
}

dynplaylist_ptr
GlobalActionManager::loadDynamicPlaylist( const QUrl& url, bool station )
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific station command:" << url.toString();
        return dynplaylist_ptr();
    }

    if( parts[ 0 ] == "create" ) {
        if( !url.hasQueryItem( "title" ) || !url.hasQueryItem( "type" ) ) {
            tLog() << "Station create command needs title and type..." << url.toString();
            return dynplaylist_ptr();
        }
        QString title = url.queryItemValue( "title" );
        QString type = url.queryItemValue( "type" );
        GeneratorMode m = Static;
        if( station )
            m = OnDemand;

        dynplaylist_ptr pl = DynamicPlaylist::create( SourceList::instance()->getLocal(), uuid(), title, QString(), QString(), m, false, type );
        pl->setMode( m );
        QList< dyncontrol_ptr > controls;
        QPair< QString, QString > param;
        foreach( param, url.queryItems() ) {
            if( param.first == "artist" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Artist" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistRadioType ) );
                controls << c;
            } else if( param.first == "artist_limitto" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Artist" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistType ) );
                controls << c;
            } else if( param.first == "description" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Artist Description" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistDescriptionType ) );
                controls << c;
            } else if( param.first == "variety" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Variety" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::Variety ) );
                controls << c;
            } else if( param.first.startsWith( "tempo" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Tempo" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::MinTempo + extra ) );
                controls << c;
            } else if( param.first.startsWith( "duration" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Duration" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::MinDuration + extra ) );
                controls << c;
            } else if( param.first.startsWith( "loudness" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Loudness" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::MinLoudness + extra ) );
                controls << c;
            } else if( param.first.startsWith( "danceability" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Danceability" );
                int extra = param.first.endsWith( "_max" ) ? 1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::MinDanceability + extra ) );
                controls << c;
            } else if( param.first.startsWith( "energy" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Energy" );
                int extra = param.first.endsWith( "_max" ) ? 1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::MinEnergy + extra ) );
                controls << c;
            } else if( param.first.startsWith( "artist_familiarity" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Artist Familiarity" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistMinFamiliarity + extra ) );
                controls << c;
            } else if( param.first.startsWith( "artist_hotttnesss" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Artist Hotttnesss" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistMinHotttnesss + extra ) );
                controls << c;
            } else if( param.first.startsWith( "song_hotttnesss" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Song Hotttnesss" );
                int extra = param.first.endsWith( "_max" ) ? -1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::SongMinHotttnesss + extra ) );
                controls << c;
            } else if( param.first.startsWith( "longitude" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Longitude" );
                int extra = param.first.endsWith( "_max" ) ? 1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistMinLongitude + extra ) );
                controls << c;
            } else if( param.first.startsWith( "latitude" ) ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Latitude" );
                int extra = param.first.endsWith( "_max" ) ? 1 : 0;
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::ArtistMinLatitude + extra ) );
                controls << c;
            } else if( param.first == "key" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Key" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::Key ) );
                controls << c;
            } else if( param.first == "mode" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Mode" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::Mode ) );
                controls << c;
            } else if( param.first == "mood" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Mood" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::Mood ) );
                controls << c;
            } else if( param.first == "style" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Style" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::Style ) );
                controls << c;
            } else if( param.first == "song" ) {
                dyncontrol_ptr c = pl->generator()->createControl( "Song" );
                c->setInput( param.second );
                c->setMatch( QString::number( (int)Echonest::DynamicPlaylist::SongRadioType ) );
                controls << c;
            }
        }
        if( m == OnDemand )
            pl->createNewRevision( uuid(), pl->currentrevision(), type, controls );
        else
            pl->createNewRevision( uuid(), pl->currentrevision(), type, controls, pl->entries() );

        return pl;
    }

    return dynplaylist_ptr();
}


bool
GlobalActionManager::handleStationCommand( const QUrl& url )
{
    return !loadDynamicPlaylist( url, true ).isNull();
}

bool
GlobalActionManager::handlePlayCommand( const QUrl& url )
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific play command:" << url.toString();
        return false;
    }

    if( parts[ 0 ] == "track" ) {
        if( playSpotify( url ) )
            return true;

        QPair< QString, QString > pair;
        QString title, artist, album, urlStr;
        foreach( pair, url.queryItems() ) {
            if( pair.first == "title" )
                title = pair.second;
            else if( pair.first == "artist" )
                artist = pair.second;
            else if( pair.first == "album" )
                album = pair.second;
            else if( pair.first == "url" )
                urlStr = pair.second;
        }
        query_ptr q = Query::get( artist, title, album );
        if( !urlStr.isEmpty() )
            q->setResultHint( urlStr );
        Pipeline::instance()->resolve( q, true );

        m_waitingToPlay = q;
        connect( q.data(), SIGNAL( resolvingFinished( bool ) ), this, SLOT( waitingForResolved( bool ) ) );

        return true;
    }

    return false;
}

bool
GlobalActionManager::playSpotify( const QUrl& url )
{
    if( !url.hasQueryItem( "spotifyURI" ) && !url.hasQueryItem( "spotifyURL" ) )
        return false;

    QString spotifyUrl = url.hasQueryItem( "spotifyURI" ) ? url.queryItemValue( "spotifyURI" ) : url.queryItemValue( "spotifyURL" );
    SpotifyParser* p = new SpotifyParser( spotifyUrl, this );
    connect( p, SIGNAL( track( Tomahawk::query_ptr ) ), this, SLOT( spotifyToPlay( Tomahawk::query_ptr ) ) );

    return true;
}

void
GlobalActionManager::spotifyToPlay( const query_ptr& q )
{
    Pipeline::instance()->resolve( q, true );

    m_waitingToPlay = q;
    connect( q.data(), SIGNAL( resolvingFinished( bool ) ), this, SLOT( waitingForResolved( bool ) ) );
}


bool GlobalActionManager::handleBookmarkCommand(const QUrl& url)
{
    QStringList parts = url.path().split( "/" ).mid( 1 ); // get the rest of the command
    if( parts.isEmpty() ) {
        tLog() << "No specific bookmark command:" << url.toString();
        return false;
    }

    if( parts[ 0 ] == "track" ) {
        QPair< QString, QString > pair;
        QString title, artist, album, urlStr;
        foreach( pair, url.queryItems() ) {
            if( pair.first == "title" )
                title = pair.second;
            else if( pair.first == "artist" )
                artist = pair.second;
            else if( pair.first == "album" )
                album = pair.second;
            else if( pair.first == "url" )
                urlStr = pair.second;
        }
        query_ptr q = Query::get( artist, title, album );
        if( !urlStr.isEmpty() )
            q->setResultHint( urlStr );
        Pipeline::instance()->resolve( q, true );

        // now we add it to the special "bookmarks" playlist, creating it if it doesn't exist. if nothing is playing, start playing the track
        QSharedPointer< LocalCollection > col = SourceList::instance()->getLocal()->collection().dynamicCast< LocalCollection >();
        playlist_ptr bookmarkpl = col->bookmarksPlaylist();
        if( bookmarkpl.isNull() ) { // create it and do the deed then
            m_waitingToBookmark = q;
            col->createBookmarksPlaylist();
            connect( col.data(), SIGNAL( bookmarkPlaylistCreated( Tomahawk::playlist_ptr ) ), this, SLOT( bookmarkPlaylistCreated( Tomahawk::playlist_ptr ) ), Qt::UniqueConnection );
        } else {
            doBookmark( bookmarkpl, q );
        }

        return true;
    }

    return false;
}



void
GlobalActionManager::bookmarkPlaylistCreated( const playlist_ptr& pl )
{
    Q_ASSERT( !m_waitingToBookmark.isNull() );
    doBookmark( pl, m_waitingToBookmark );
}

void
GlobalActionManager::doBookmark( const playlist_ptr& pl, const query_ptr& q )
{
    plentry_ptr e( new PlaylistEntry );
    e->setGuid( uuid() );

    if ( q->results().count() )
        e->setDuration( q->results().at( 0 )->duration() );
    else
        e->setDuration( 0 );

    e->setLastmodified( 0 );
    e->setAnnotation( "" ); // FIXME
    e->setQuery( q );

    pl->createNewRevision( uuid(), pl->currentrevision(), QList< plentry_ptr >( pl->entries() ) << e );
    connect( pl.data(), SIGNAL( revisionLoaded( Tomahawk::PlaylistRevision ) ), this, SLOT( showPlaylist() ) );

    m_toShow = pl;

    m_waitingToBookmark.clear();
}

void
GlobalActionManager::showPlaylist()
{
    if( m_toShow.isNull() )
        return;

    ViewManager::instance()->show( m_toShow );

    m_toShow.clear();
}

void
GlobalActionManager::waitingForResolved( bool /* success */ )
{
    if ( m_waitingToPlay.data() != sender() )
    {
        m_waitingToPlay.clear();
        return;
    }

    if ( !m_waitingToPlay.isNull() && m_waitingToPlay->playable() )
    {
        // play it!
//         AudioEngine::instance()->playItem( AudioEngine::instance()->playlist(), m_waitingToPlay->results().first() );
        AudioEngine::instance()->play();

        m_waitingToPlay.clear();
    }
}

QString
GlobalActionManager::hostname() const
{
    return QString( "http://toma.hk" );
}

/// QMIMEDATA HANDLING

QStringList
GlobalActionManager::mimeTypes() const
{
    return m_mimeTypes;
}


bool
GlobalActionManager::acceptsMimeData( const QMimeData* data, bool tracksOnly )
{
    if ( data->hasFormat( "application/tomahawk.query.list" )
        || data->hasFormat( "application/tomahawk.plentry.list" )
        || data->hasFormat( "application/tomahawk.result.list" )
        || data->hasFormat( "application/tomahawk.metadata.album" )
        || data->hasFormat( "application/tomahawk.metadata.artist" ) )
    {
        return true;
    }

    // crude check for spotify tracks
    if ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "spotify" ) &&
       ( tracksOnly ? data->data( "text/plain" ).contains( "track" ) : true ) )
        return true;

    // crude check for rdio tracks
    if ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "rdio.com" ) &&
        ( tracksOnly ? data->data( "text/plain" ).contains( "track" ) : true ) )
        return true;

    // We whitelist t.co and bit.ly (and j.mp) since they do some link checking. Often playable (e.g. spotify..) links hide behind them,
    //  so we do an extra level of lookup
    if ( ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "bit.ly" ) ) ||
         ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "j.mp" ) ) ||
         ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "t.co" ) ) ||
         ( data->hasFormat( "text/plain" ) && data->data( "text/plain" ).contains( "rd.io" ) ) )
        return true;

    return false;
}


void
GlobalActionManager::tracksFromMimeData( const QMimeData* data )
{
    if ( data->hasFormat( "application/tomahawk.query.list" ) )
        emit tracks( tracksFromQueryList( data ) );
    else if ( data->hasFormat( "application/tomahawk.result.list" ) )
        emit tracks( tracksFromResultList( data ) );
    else if ( data->hasFormat( "application/tomahawk.metadata.album" ) )
        emit tracks( tracksFromAlbumMetaData( data ) );
    else if ( data->hasFormat( "application/tomahawk.metadata.artist" ) )
        emit tracks( tracksFromArtistMetaData( data ) );
    else if ( data->hasFormat( "text/plain" ) )
    {
        QString plainData = QString::fromUtf8( data->data( "text/plain" ).constData() );
        tDebug() << "Got text/plain mime data:" << data->data( "text/plain" ) << "decoded to:" << plainData;
        handleTrackUrls ( plainData );
    }
}

void
GlobalActionManager::handleTrackUrls( const QString& urls )
{
    if ( urls.contains( "open.spotify.com/track") ||
         urls.contains( "spotify:track" ) )
    {
        QStringList tracks = urls.split( "\n" );

        tDebug() << "Got a list of spotify urls!" << tracks;
        SpotifyParser* spot = new SpotifyParser( tracks, this );
        connect( spot, SIGNAL( tracks( QList<Tomahawk::query_ptr> ) ), this, SIGNAL( tracks( QList<Tomahawk::query_ptr> ) ) );
    } else if ( urls.contains( "rdio.com" ) )
    {
        QStringList tracks = urls.split( "\n" );

        tDebug() << "Got a list of rdio urls!" << tracks;
        RdioParser* rdio = new RdioParser( this );
        connect( rdio, SIGNAL( tracks( QList<Tomahawk::query_ptr> ) ), this, SIGNAL( tracks( QList<Tomahawk::query_ptr> ) ) );
        rdio->parse( tracks );
    } else if ( urls.contains( "bit.ly" ) ||
                urls.contains( "j.mp" ) ||
                urls.contains( "t.co" ) ||
                urls.contains( "rd.io" ) )
    {
        QStringList tracks = urls.split( "\n" );

        tDebug() << "Got a list of shortened urls!" << tracks;
        ShortenedLinkParser* parser = new ShortenedLinkParser( tracks, this );
        connect( parser, SIGNAL( urls( QStringList ) ), this, SLOT( expandedUrls( QStringList ) ) );
    }
}


void
GlobalActionManager::expandedUrls( QStringList urls )
{
    handleTrackUrls( urls.join( "\n" ) );
}


QList< query_ptr >
GlobalActionManager::tracksFromQueryList( const QMimeData* data )
{
    QList< query_ptr > queries;
    QByteArray itemData = data->data( "application/tomahawk.query.list" );
    QDataStream stream( &itemData, QIODevice::ReadOnly );

    while ( !stream.atEnd() )
    {
        qlonglong qptr;
        stream >> qptr;

        query_ptr* query = reinterpret_cast<query_ptr*>(qptr);
        if ( query && !query->isNull() )
        {
            tDebug() << "Dropped query item:" << query->data()->artist() << "-" << query->data()->track();
            queries << *query;
        }
    }

    return queries;
}

QList< query_ptr >
GlobalActionManager::tracksFromResultList( const QMimeData* data )
{
    QList< query_ptr > queries;
    QByteArray itemData = data->data( "application/tomahawk.result.list" );
    QDataStream stream( &itemData, QIODevice::ReadOnly );

    while ( !stream.atEnd() )
    {
        qlonglong qptr;
        stream >> qptr;

        result_ptr* result = reinterpret_cast<result_ptr*>(qptr);
        if ( result && !result->isNull() )
        {
            tDebug() << "Dropped result item:" << result->data()->artist()->name() << "-" << result->data()->track();
            query_ptr q = result->data()->toQuery();
            q->addResults( QList< result_ptr >() << *result );
            queries << q;
        }
    }

    return queries;
}

QList< query_ptr >
GlobalActionManager::tracksFromAlbumMetaData( const QMimeData *data )
{
    QList<query_ptr> queries;
    QByteArray itemData = data->data( "application/tomahawk.metadata.album" );
    QDataStream stream( &itemData, QIODevice::ReadOnly );

    while ( !stream.atEnd() )
    {
        QString artist;
        stream >> artist;
        QString album;
        stream >> album;

        artist_ptr artistPtr = Artist::get( artist );
        album_ptr albumPtr = Album::get( artistPtr, album );
        queries << albumPtr->tracks();
    }
    return queries;
}

QList< query_ptr >
GlobalActionManager::tracksFromArtistMetaData( const QMimeData *data )
{
    QList<query_ptr> queries;
    QByteArray itemData = data->data( "application/tomahawk.metadata.artist" );
    QDataStream stream( &itemData, QIODevice::ReadOnly );

    while ( !stream.atEnd() )
    {
        QString artist;
        stream >> artist;

        artist_ptr artistPtr = Artist::get( artist );
        queries << artistPtr->tracks();
    }
    return queries;
}

/// SPOTIFY URL HANDLING

bool
GlobalActionManager::openSpotifyLink( const QString& link )
{
    SpotifyParser* spot = new SpotifyParser( link, this );
    connect( spot, SIGNAL( track( Tomahawk::query_ptr ) ), this, SLOT( handleOpenTrack( Tomahawk::query_ptr ) ) );

    return true;
}

bool
GlobalActionManager::openRdioLink( const QString& link )
{
    RdioParser* rdio = new RdioParser( this );
    connect( rdio, SIGNAL( track( Tomahawk::query_ptr ) ), this, SLOT( handleOpenTrack( Tomahawk::query_ptr ) ) );
    rdio->parse( link );

    return true;
}

