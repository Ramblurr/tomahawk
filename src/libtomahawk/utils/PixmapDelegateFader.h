/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2011-2012, Leo Franchi <lfranchi@kde.org>
 *   Copyright 2012, Jeff Mitchell <jeffe@tomahawk-player.org>
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

#ifndef PIXMAPDELEGATEFADER_H
#define PIXMAPDELEGATEFADER_H

#include "artist.h"
#include "album.h"
#include "query.h"

#include <QObject>
#include <QTimeLine>
#include <QQueue>
#include <QWeakPointer>

namespace Tomahawk
{

/**
 * No parent, manage it yourself!
 *
 * TODO: Handle changing sizes
 */

class PixmapDelegateFader : public QObject
{
    Q_OBJECT

    static QWeakPointer< TomahawkUtils::SharedTimeLine > stlInstance();
    
public:
    PixmapDelegateFader( const artist_ptr& artist, const QSize& size, TomahawkUtils::ImageMode mode = TomahawkUtils::Original, bool forceLoad = true );
    PixmapDelegateFader( const album_ptr& album, const QSize& size, TomahawkUtils::ImageMode mode = TomahawkUtils::Original, bool forceLoad = true );
    PixmapDelegateFader( const query_ptr& track, const QSize& size, TomahawkUtils::ImageMode mode = TomahawkUtils::Original, bool forceLoad = true );

    virtual ~PixmapDelegateFader();
    
    QPixmap currentPixmap() const;

signals:
    void repaintRequest();

private slots:
    void artistChanged();
    void albumChanged();
    void trackChanged();

    void onAnimationStep( int );
    void onAnimationFinished();
    void setPixmap( const QPixmap& pixmap );

private:
    void init();

    artist_ptr m_artist;
    album_ptr m_album;
    query_ptr m_track;
    QSize m_size;
    TomahawkUtils::ImageMode m_mode;
    int m_startFrame;
    bool m_connectedToStl;
    float m_fadePct;
    QString m_oldImageMd5;
    
    QQueue<QPixmap> m_pixmapQueue;
    
    QPixmap m_currentReference, m_oldReference, m_current;

    static QWeakPointer< TomahawkUtils::SharedTimeLine > s_stlInstance;
};

}

#endif // PIXMAPDELEGATEFADER_H
