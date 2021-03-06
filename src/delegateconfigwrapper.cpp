/* === This file is part of Tomahawk Player - <http://tomahawk-player.org> ===
 *
 *   Copyright 2010-2011, Leo Franchi <lfranchi@kde.org>
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
#include "delegateconfigwrapper.h"


DelegateConfigWrapper::DelegateConfigWrapper( QWidget* conf, const QString& title, QWidget* parent, Qt::WindowFlags flags )
    : QDialog( parent, flags )
    , m_widget( conf )
    , m_deleted( false )
{
    m_widget->setWindowFlags( Qt::Sheet );
#ifdef Q_WS_MAC
    m_widget->setVisible( true );
#endif
    setWindowTitle( title );
    QVBoxLayout* v = new QVBoxLayout( this );
    v->setContentsMargins( 0, 0, 0, 0 );
    v->addWidget( m_widget );

    m_buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this );
    m_okButton = m_buttons->button( QDialogButtonBox::Ok );
    connect( m_buttons, SIGNAL( clicked( QAbstractButton*)  ), this, SLOT( closed( QAbstractButton* ) ) );
    connect( this, SIGNAL( rejected() ), this, SLOT( rejected() ) );
    v->addWidget( m_buttons );

    setLayout( v );

#ifdef Q_WS_MAC
    setSizeGripEnabled( false );
    setMinimumSize( sizeHint() );
    setMaximumSize( sizeHint() ); // to remove the resize grip on osx this is the only way

    if( conf->metaObject()->indexOfSignal( "sizeHintChanged()" ) > -1 )
        connect( conf, SIGNAL( sizeHintChanged() ), this, SLOT( updateSizeHint() ) );
#else
    m_widget->setVisible( true );
#endif

}


void
DelegateConfigWrapper::setShowDelete( bool del )
{
    if ( del )
        m_deleteButton = m_buttons->addButton( tr( "Delete Account" ), QDialogButtonBox::DestructiveRole );
}


void
DelegateConfigWrapper::toggleOkButton( bool dataError )
{
    // if dataError is True we want to set the button enabled to false
    m_okButton->setEnabled( !dataError );
}


void
DelegateConfigWrapper::closed( QAbstractButton* b )
{
    // let the config widget live to see another day
    layout()->removeWidget( m_widget );
    m_widget->setParent( 0 );
    m_widget->setVisible( false );

    QDialogButtonBox* buttons = qobject_cast< QDialogButtonBox* >( sender() );
    if ( buttons->standardButton( b ) == QDialogButtonBox::Ok )
        done( QDialog::Accepted );
    else if ( b == m_deleteButton )
    {
        m_deleted = true;
        emit closedWithDelete();
        reject();
    }
    else
        done( QDialog::Rejected );
}


void
DelegateConfigWrapper::rejected()
{
    layout()->removeWidget( m_widget );
    m_widget->setParent( 0 );
    m_widget->setVisible( false );
}


void
DelegateConfigWrapper::updateSizeHint()
{
    hide();
    setSizeGripEnabled( false );
    setMinimumSize( sizeHint() );
    setMaximumSize( sizeHint() );

    show();
}

