/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/stream.cpp
// Purpose:     wxStream base classes
// Author:      Guilhem Lavaux
// Modified by: VZ (23.11.00) to fix realloc()ing new[]ed memory,
//                            general code review
// Created:     11/07/98
// RCS-ID:      $Id$
// Copyright:   (c) Guilhem Lavaux
// Licence:     wxWindows license
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#ifdef __GNUG__
    #pragma implementation "stream.h"
#endif

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
  #pragma hdrstop
#endif

#ifndef WX_PRECOMP
  #include "wx/defs.h"
#endif

#if wxUSE_STREAMS

#include <ctype.h>
#include "wx/stream.h"
#include "wx/datstrm.h"
#include "wx/textfile.h"
#include "wx/log.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// the temporary buffer size used when copying from stream to stream
#define BUF_TEMP_SIZE 10000

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxStreamBuffer
// ----------------------------------------------------------------------------

void wxStreamBuffer::SetError(wxStreamError err)
{
   if ( m_stream->m_lasterror == wxStream_NOERROR )
       m_stream->m_lasterror = err;
}

void wxStreamBuffer::InitBuffer()
{
    m_buffer_start =
    m_buffer_end =
    m_buffer_pos = NULL;
    m_buffer_size = 0;

    // if we are going to allocate the buffer, we should free it later as well
    m_destroybuf = TRUE;
}

void wxStreamBuffer::Init()
{
    InitBuffer();

    m_fixed = TRUE;
}

wxStreamBuffer::wxStreamBuffer(wxStreamBase& stream, BufMode mode)
{
    Init();

    m_stream = &stream;
    m_mode = mode;

    m_flushable = TRUE;
    m_destroystream = FALSE;
}

wxStreamBuffer::wxStreamBuffer(BufMode mode)
{
    Init();

    wxASSERT_MSG(mode != read_write, wxT("you have to use the other ctor for read_write mode") );
    if ( mode == read )
        m_stream = new wxInputStream;
    else if ( mode == write)
        m_stream = new wxOutputStream;
    else
        m_stream = NULL;

    m_mode = mode;

    m_flushable = FALSE;
    m_destroystream = TRUE;
}

wxStreamBuffer::wxStreamBuffer(const wxStreamBuffer& buffer)
{
    // doing this has big chances to lead to a crashwhen the source buffer is
    // destroyed (otherwise assume the caller knows what he does)
    wxASSERT_MSG( !buffer.m_destroybuf && !buffer.m_destroystream,
                  _T("it's a bad idea to copy this buffer") );

    m_buffer_start = buffer.m_buffer_start;
    m_buffer_end = buffer.m_buffer_end;
    m_buffer_pos = buffer.m_buffer_pos;
    m_buffer_size = buffer.m_buffer_size;
    m_fixed = buffer.m_fixed;
    m_flushable = buffer.m_flushable;
    m_stream = buffer.m_stream;
    m_mode = buffer.m_mode;
    m_destroybuf = FALSE;
    m_destroystream = FALSE;
}

void wxStreamBuffer::FreeBuffer()
{
    if ( m_destroybuf )
        free(m_buffer_start);
}

wxStreamBuffer::~wxStreamBuffer()
{
    FreeBuffer();

    if ( m_destroystream )
        delete m_stream;
}

wxInputStream *wxStreamBuffer::GetInputStream() const
{
    return m_mode == write ? NULL : (wxInputStream *)m_stream;
}

wxOutputStream *wxStreamBuffer::GetOutputStream() const
{
    return m_mode == read ? NULL : (wxOutputStream *)m_stream;
}

void wxStreamBuffer::SetBufferIO(void *buffer_start,
                                 void *buffer_end,
                                 bool takeOwnership)
{
    SetBufferIO(buffer_start, (char *)buffer_end - (char *)buffer_start,
                takeOwnership);
}

void wxStreamBuffer::SetBufferIO(void *start,
                                 size_t len,
                                 bool takeOwnership)
{
    // start by freeing the old buffer
    FreeBuffer();

    m_buffer_start = (char *)start;
    m_buffer_end   = m_buffer_start + len;

    m_buffer_size = len;

    // if we own it, we free it
    m_destroybuf = takeOwnership;

    ResetBuffer();
}

void wxStreamBuffer::SetBufferIO(size_t bufsize)
{
    // start by freeing the old buffer
    FreeBuffer();

    if ( bufsize )
    {
        SetBufferIO(malloc(bufsize), bufsize, TRUE /* take ownership */);
    }
    else // no buffer size => no buffer
    {
        InitBuffer();
    }
}

void wxStreamBuffer::ResetBuffer()
{
    wxCHECK_RET( m_stream, _T("should have a stream in wxStreamBuffer") );

    m_stream->m_lasterror = wxStream_NOERROR;
    m_stream->m_lastcount = 0;
    if (m_mode == read && m_flushable)
        m_buffer_pos = m_buffer_end;
    else
        m_buffer_pos = m_buffer_start;
}

// fill the buffer with as much data as possible (only for read buffers)
bool wxStreamBuffer::FillBuffer()
{
    wxInputStream *inStream = GetInputStream();

    wxCHECK_MSG( inStream, FALSE, _T("should have a stream in wxStreamBuffer") );

    size_t count = inStream->OnSysRead(m_buffer_start, m_buffer_size);
    if ( !count )
        return FALSE;

    m_buffer_end = m_buffer_start + count;
    m_buffer_pos = m_buffer_start;

    return TRUE;
}

// write the buffer contents to the stream (only for write buffers)
bool wxStreamBuffer::FlushBuffer()
{
    wxCHECK_MSG( m_flushable, FALSE, _T("can't flush this buffer") );

    // FIXME: what is this check for? (VZ)
    if ( m_buffer_pos == m_buffer_start )
        return FALSE;

    wxOutputStream *outStream = GetOutputStream();

    wxCHECK_MSG( outStream, FALSE, _T("should have a stream in wxStreamBuffer") );

    size_t current = m_buffer_pos - m_buffer_start;
    size_t count = outStream->OnSysWrite(m_buffer_start, current);
    if ( count != current )
        return FALSE;

    m_buffer_pos = m_buffer_start;

    return TRUE;
}

size_t wxStreamBuffer::GetDataLeft()
{
    /* Why is this done? RR. */
    if ( m_buffer_pos == m_buffer_end && m_flushable)
        FillBuffer();

    return GetBytesLeft();
}

// copy up to size bytes from our buffer into the provided one
void wxStreamBuffer::GetFromBuffer(void *buffer, size_t size)
{
    // don't get more bytes than left in the buffer
    size_t left = GetBytesLeft();

    if ( size > left )
        size = left;

    memcpy(buffer, m_buffer_pos, size);
    m_buffer_pos += size;
}

// copy the contents of the provided buffer into this one
void wxStreamBuffer::PutToBuffer(const void *buffer, size_t size)
{
    size_t left = GetBytesLeft();
    if ( size > left )
    {
        if ( m_fixed )
        {
            // we can't realloc the buffer, so just copy what we can
            size = left;
        }
        else // !m_fixed
        {
            // realloc the buffer to have enough space for the data
            size_t delta = m_buffer_pos - m_buffer_start;

            char *startOld = m_buffer_start;
            m_buffer_size += size;
            m_buffer_start = (char *)realloc(m_buffer_start, m_buffer_size);
            if ( !m_buffer_start )
            {
                // don't leak memory if realloc() failed
                m_buffer_start = startOld;
                m_buffer_size -= size;

                // what else can we do?
                return;
            }

            // adjust the pointers invalidated by realloc()
            m_buffer_pos = m_buffer_start + delta;
            m_buffer_end = m_buffer_start + m_buffer_size;
        }
    }

    memcpy(m_buffer_pos, buffer, size);
    m_buffer_pos += size;
}

void wxStreamBuffer::PutChar(char c)
{
    wxOutputStream *outStream = GetOutputStream();

    wxCHECK_RET( outStream, _T("should have a stream in wxStreamBuffer") );

    // if we don't have buffer at all, just forward this call to the stream,
    if ( !HasBuffer() )
    {
        outStream->OnSysWrite(&c, 1);
    }
    else
    {
        // otherwise check we have enough space left
        if ( !GetDataLeft() && !FlushBuffer() )
        {
            // we don't
            SetError(wxStream_WRITE_ERR);
        }
        else
        {
            PutToBuffer(&c, 1);
            m_stream->m_lastcount = 1;
        }
    }
}

char wxStreamBuffer::Peek()
{
    wxCHECK_MSG( m_stream && HasBuffer(), 0,
                 _T("should have the stream and the buffer in wxStreamBuffer") );

    if ( !GetDataLeft() )
    {
        SetError(wxStream_READ_ERR);
        return 0;
    }

    char c;
    GetFromBuffer(&c, 1);
    m_buffer_pos--;

    return c;
}

char wxStreamBuffer::GetChar()
{
    wxInputStream *inStream = GetInputStream();

    wxCHECK_MSG( inStream, 0, _T("should have a stream in wxStreamBuffer") );

    char c;
    if ( !HasBuffer() )
    {
        inStream->OnSysRead(&c, 1);
    }
    else
    {
        if ( !GetDataLeft() )
        {
            SetError(wxStream_READ_ERR);
            c = 0;
        }
        else
        {
            GetFromBuffer(&c, 1);
            m_stream->m_lastcount = 1;
        }
    }

    return c;
}

size_t wxStreamBuffer::Read(void *buffer, size_t size)
{
    wxInputStream *inStream = GetInputStream();

    wxCHECK_MSG( inStream, 0, _T("should have a stream in wxStreamBuffer") );

    // lasterror is reset before all new IO calls
    m_stream->m_lasterror = wxStream_NOERROR;

    if ( !HasBuffer() )
    {
        m_stream->m_lastcount = inStream->OnSysRead(buffer, size);
    }
    else // we have a buffer, use it
    {
        size_t orig_size = size;

        while ( size > 0 )
        {
            size_t left = GetDataLeft();

            // if the requested number of bytes if greater than the buffer
            // size, read data in chunks
            if ( size > left )
            {
                GetFromBuffer(buffer, left);
                size -= left;
                buffer = (char *)buffer + left;

                if ( !FillBuffer() )
                {
                    SetError(wxStream_EOF);
                    break;
                }
            }
            else // otherwise just do it in one gulp
            {
                GetFromBuffer(buffer, size);
                size = 0;
            }
        }

        m_stream->m_lastcount = orig_size - size;
    }

    return m_stream->m_lastcount;
}

// this should really be called "Copy()"
size_t wxStreamBuffer::Read(wxStreamBuffer *dbuf)
{
    wxCHECK_MSG( m_mode != write, 0, _T("can't read from this buffer") );

    char buf[BUF_TEMP_SIZE];
    size_t nRead,
           total = 0;

    do
    {
        nRead = Read(dbuf, WXSIZEOF(buf));
        if ( nRead )
        {
            nRead = dbuf->Write(buf, nRead);
            total += nRead;
        }
    }
    while ( nRead );

    return total;
}

size_t wxStreamBuffer::Write(const void *buffer, size_t size)
{
    wxOutputStream *outStream = GetOutputStream();

    wxCHECK_MSG( outStream, 0, _T("should have a stream in wxStreamBuffer") );

    // lasterror is reset before all new IO calls
    m_stream->m_lasterror = wxStream_NOERROR;

    if ( !HasBuffer() && m_fixed )
    {
        // no buffer, just forward the call to the stream
        m_stream->m_lastcount = outStream->OnSysWrite(buffer, size);
    }
    else // we [may] have a buffer, use it
    {
        size_t orig_size = size;

        while ( size > 0 )
        {
            size_t left = GetBytesLeft();

            // if the buffer is too large to fit in the stream buffer, split
            // it in smaller parts
            //
            // NB: If stream buffer isn't fixed (as for wxMemoryOutputStream),
            //     we always go to the second case.
            //
            // FIXME: fine, but if it fails we should (re)try writing it by
            //        chunks as this will (hopefully) always work (VZ)
            if ( size > left && m_fixed )
            {
                PutToBuffer(buffer, left);
                size -= left;
                buffer = (char *)buffer + left;

                if ( !FlushBuffer() )
                {
                    SetError(wxStream_WRITE_ERR);

                    break;
                }

                m_buffer_pos = m_buffer_start;
            }
            else // we can do it in one gulp
            {
                PutToBuffer(buffer, size);
                size = 0;
            }
        }

        m_stream->m_lastcount = orig_size - size;
    }

    return m_stream->m_lastcount;
}

size_t wxStreamBuffer::Write(wxStreamBuffer *sbuf)
{
    wxCHECK_MSG( m_mode != read, 0, _T("can't write to this buffer") );
    wxCHECK_MSG( sbuf->m_mode != write, 0, _T("can't read from that buffer") );

    char buf[BUF_TEMP_SIZE];
    size_t nWrite,
           total = 0;

    do
    {
        size_t nRead = sbuf->Read(buf, WXSIZEOF(buf));
        if ( nRead )
        {
            nWrite = Write(buf, nRead);
            if ( nWrite < nRead )
            {
                // put back data we couldn't copy
                wxInputStream *in_stream = (wxInputStream *)sbuf->GetStream();

                in_stream->Ungetch(buf + nWrite, nRead - nWrite);
            }

            total += nWrite;
        }
        else
        {
            nWrite = 0;
        }
    }
    while ( nWrite == WXSIZEOF(buf) );

    return total;
}

off_t wxStreamBuffer::Seek(off_t pos, wxSeekMode mode)
{
    off_t ret_off, diff;

    off_t last_access = GetLastAccess();

    if ( !m_flushable )
    {
        switch (mode)
        {
            case wxFromStart:
                diff = pos;
                break;

            case wxFromCurrent:
                diff = pos + GetIntPosition();
                break;

            case wxFromEnd:
                diff = pos + last_access;
                break;

            default:
                wxFAIL_MSG( _T("invalid seek mode") );

                return wxInvalidOffset;
        }
        if (diff < 0 || diff > last_access)
            return wxInvalidOffset;
        SetIntPosition(diff);
        return diff;
    }

    switch ( mode )
    {
        case wxFromStart:
            // We'll try to compute an internal position later ...
            ret_off = m_stream->OnSysSeek(pos, wxFromStart);
            ResetBuffer();
            return ret_off;

        case wxFromCurrent:
            diff = pos + GetIntPosition();

            if ( (diff > last_access) || (diff < 0) )
            {
                // We must take into account the fact that we have read
                // something previously.
                ret_off = m_stream->OnSysSeek(diff-last_access, wxFromCurrent);
                ResetBuffer();
                return ret_off;
            }
            else
            {
                SetIntPosition(diff);
                return pos;
            }

        case wxFromEnd:
            // Hard to compute: always seek to the requested position.
            ret_off = m_stream->OnSysSeek(pos, wxFromEnd);
            ResetBuffer();
            return ret_off;
    }

    return wxInvalidOffset;
}

off_t wxStreamBuffer::Tell() const
{
    off_t pos;

    // only ask the stream for position if we have a real stream and not a
    // dummy one which we created ourselves, otherwise we'd call
    // wxStream::OnSysTell() which would always return wxInvalidOffset
    if ( !m_destroystream )
    {
        pos = m_stream->OnSysTell();
        if ( pos == wxInvalidOffset )
            return wxInvalidOffset;
    }
    else // no associated stream
    {
        pos = 0;
    }

    pos += GetIntPosition();

    if ( m_mode == read && m_flushable )
        pos -= GetLastAccess();

    return pos;
}

// ----------------------------------------------------------------------------
// wxStreamBase
// ----------------------------------------------------------------------------

wxStreamBase::wxStreamBase()
{
    m_lasterror = wxStream_NOERROR;
    m_lastcount = 0;
}

wxStreamBase::~wxStreamBase()
{
}

off_t wxStreamBase::OnSysSeek(off_t WXUNUSED(seek), wxSeekMode WXUNUSED(mode))
{
    return wxInvalidOffset;
}

off_t wxStreamBase::OnSysTell() const
{
    return wxInvalidOffset;
}

// ----------------------------------------------------------------------------
// wxInputStream
// ----------------------------------------------------------------------------

wxInputStream::wxInputStream()
{
    m_wback = NULL;
    m_wbacksize =
    m_wbackcur = 0;
}

wxInputStream::~wxInputStream()
{
    free(m_wback);
}

size_t wxInputStream::OnSysRead(void * WXUNUSED(buffer),
                                size_t WXUNUSED(bufsize))
{
    return 0;
}

bool wxInputStream::Eof() const
{
    wxInputStream *self = wxConstCast(this, wxInputStream);

    char c;
    self->Read(&c, 1);

    // some streams can know that they're at EOF before actually trying to
    // read beyond the end of stream (e.g. files) while others have no way of
    // knowing it, so to provide the same behaviour in all cases we only
    // return TRUE from here if the character really couldn't be read
    if ( !self->LastRead() && GetLastError() == wxSTREAM_EOF )
    {
        return TRUE;
    }

    self->Ungetch(c);

    return FALSE;
}

char *wxInputStream::AllocSpaceWBack(size_t needed_size)
{
    // get number of bytes left from previous wback buffer
    size_t toget = m_wbacksize - m_wbackcur;

    // allocate a buffer large enough to hold prev + new data
    char *temp_b = (char *)malloc(needed_size + toget);

    if (!temp_b)
        return NULL;

    // copy previous data (and free old buffer) if needed
    if (m_wback)
    {
        memmove(temp_b + needed_size, m_wback + m_wbackcur, toget);
        free(m_wback);
    }

    // done
    m_wback = temp_b;
    m_wbackcur = 0;
    m_wbacksize = needed_size + toget;

    return m_wback;
}

size_t wxInputStream::GetWBack(void *buf, size_t bsize)
{
    if (!m_wback)
        return 0;

    // how many bytes do we have in the buffer?
    size_t toget = m_wbacksize - m_wbackcur;

    if ( bsize < toget )
    {
        // we won't read everything
        toget = bsize;
    }

    // copy the data from the cache 
    memcpy(buf, m_wback + m_wbackcur, toget);

    m_wbackcur += toget;
    if ( m_wbackcur == m_wbacksize )
    {
        // TODO: should we really free it here all the time? maybe keep it?
        free(m_wback);
        m_wback = NULL;
        m_wbacksize = 0;
        m_wbackcur = 0;
    }

    // return the number of bytes copied
    return toget;
}

size_t wxInputStream::Ungetch(const void *buf, size_t bufsize)
{
    if ( m_lasterror != wxSTREAM_NO_ERROR && m_lasterror != wxSTREAM_EOF )
    {
        // can't operate on this stream until the error is cleared
        return 0;
    }

    char *ptrback = AllocSpaceWBack(bufsize);
    if (!ptrback)
        return 0;

    // Eof() shouldn't return TRUE any longer
    if ( m_lasterror == wxSTREAM_EOF )
        m_lasterror = wxSTREAM_NO_ERROR;

    memcpy(ptrback, buf, bufsize);
    return bufsize;
}

bool wxInputStream::Ungetch(char c)
{
    return Ungetch(&c, sizeof(char)) != 0;
}

char wxInputStream::GetC()
{
    char c;
    Read(&c, 1);
    return c;
}

wxInputStream& wxInputStream::Read(void *buf, size_t size)
{
    char *p = (char *)buf;
    m_lastcount = 0;

    size_t read = GetWBack(buf, size);
    for ( ;; )
    {
        size -= read;
        m_lastcount += read;
        p += read;

        if ( !size )
        {
            // we read the requested amount of data
            break;
        }

        read = OnSysRead(buf, size);
        if ( !read )
        {
            // no more data available
            break;
        }
    }

    return *this;
}

char wxInputStream::Peek()
{
    char c;
    Read(&c, 1);
    if (m_lasterror == wxStream_NOERROR)
    {
        Ungetch(c);
        return c;
    }

    return 0;
}

wxInputStream& wxInputStream::Read(wxOutputStream& stream_out)
{
    char buf[BUF_TEMP_SIZE];
    size_t bytes_read = BUF_TEMP_SIZE;

    while (bytes_read == BUF_TEMP_SIZE)
    {
        bytes_read = Read(buf, bytes_read).LastRead();
        bytes_read = stream_out.Write(buf, bytes_read).LastWrite();
    }
    return *this;
}

off_t wxInputStream::SeekI(off_t pos, wxSeekMode mode)
{
    // RR: This code is duplicated in wxBufferedInputStream. This is
    // not really a good design, but buffered stream are different
    // from all other in that they handle two stream-related objects,
    // the stream buffer and parent stream.

    // I don't know whether it should be put as well in wxFileInputStream::OnSysSeek
    if (m_lasterror==wxSTREAM_EOF)
        m_lasterror=wxSTREAM_NOERROR;

    /* RR: A call to SeekI() will automatically invalidate any previous
       call to Ungetch(), otherwise it would be possible to SeekI() to
       one position, unread some bytes there, SeekI() to another position
       and the data would be corrupted.

       GRG: Could add code here to try to navigate within the wback
       buffer if possible, but is it really needed? It would only work
       when seeking in wxFromCurrent mode, else it would invalidate
       anyway... */
       
    if (m_wback)
    {
        wxLogDebug( wxT("Seeking in stream which has data written back to it.") );
        
        free(m_wback);
        m_wback = NULL;
        m_wbacksize = 0;
        m_wbackcur = 0;
    }

    return OnSysSeek(pos, mode);
}

off_t wxInputStream::TellI() const
{
    off_t pos = OnSysTell();

    if (pos != wxInvalidOffset)
        pos -= (m_wbacksize - m_wbackcur);

    return pos;
}


// ----------------------------------------------------------------------------
// wxOutputStream
// ----------------------------------------------------------------------------

wxOutputStream::wxOutputStream()
{
}

wxOutputStream::~wxOutputStream()
{
}

size_t wxOutputStream::OnSysWrite(const void * WXUNUSED(buffer),
                                  size_t WXUNUSED(bufsize))
{
    return 0;
}

void wxOutputStream::PutC(char c)
{
    Write(&c, 1);
}

wxOutputStream& wxOutputStream::Write(const void *buffer, size_t size)
{
    m_lastcount = OnSysWrite(buffer, size);
    return *this;
}

wxOutputStream& wxOutputStream::Write(wxInputStream& stream_in)
{
    stream_in.Read(*this);
    return *this;
}

off_t wxOutputStream::TellO() const
{
    return OnSysTell();
}

off_t wxOutputStream::SeekO(off_t pos, wxSeekMode mode)
{
    return OnSysSeek(pos, mode);
}

void wxOutputStream::Sync()
{
}


// ----------------------------------------------------------------------------
// wxCountingOutputStream
// ----------------------------------------------------------------------------

wxCountingOutputStream::wxCountingOutputStream ()
{
     m_currentPos = 0;
}

size_t wxCountingOutputStream::GetSize() const
{
    return m_lastcount;
}

size_t wxCountingOutputStream::OnSysWrite(const void *WXUNUSED(buffer),
                                          size_t size)
{
    m_currentPos += size;
    if (m_currentPos > m_lastcount)
        m_lastcount = m_currentPos;

    return m_currentPos;
}

off_t wxCountingOutputStream::OnSysSeek(off_t pos, wxSeekMode mode)
{
    switch ( mode )
    {
        case wxFromStart:
            m_currentPos = pos;
            break;

        case wxFromEnd:
            m_currentPos = m_lastcount + pos;
            break;

        case wxFromCurrent:
            m_currentPos += pos;
            break;

        default:
            wxFAIL_MSG( _T("invalid seek mode") );
            return wxInvalidOffset;
    }

    if (m_currentPos > m_lastcount)
        m_lastcount = m_currentPos;

    return m_currentPos;
}

off_t wxCountingOutputStream::OnSysTell() const
{
    return m_currentPos;
}

// ----------------------------------------------------------------------------
// wxFilterInputStream
// ----------------------------------------------------------------------------

wxFilterInputStream::wxFilterInputStream()
{
    m_parent_i_stream = NULL;
}

wxFilterInputStream::wxFilterInputStream(wxInputStream& stream)
{
    m_parent_i_stream = &stream;
}

wxFilterInputStream::~wxFilterInputStream()
{
}

// ----------------------------------------------------------------------------
// wxFilterOutputStream
// ----------------------------------------------------------------------------

wxFilterOutputStream::wxFilterOutputStream()
{
    m_parent_o_stream = NULL;
}

wxFilterOutputStream::wxFilterOutputStream(wxOutputStream& stream)
{
    m_parent_o_stream = &stream;
}

wxFilterOutputStream::~wxFilterOutputStream()
{
}

// ----------------------------------------------------------------------------
// wxBufferedInputStream
// ----------------------------------------------------------------------------

wxBufferedInputStream::wxBufferedInputStream(wxInputStream& s,
                                             wxStreamBuffer *buffer)
                     : wxFilterInputStream(s)
{
    if ( buffer )
    {
        // use the buffer provided by the user
        m_i_streambuf = buffer;
    }
    else // create a default buffer
    {
        m_i_streambuf = new wxStreamBuffer(*this, wxStreamBuffer::read);

        m_i_streambuf->SetBufferIO(1024);
    }
}

wxBufferedInputStream::~wxBufferedInputStream()
{
    m_parent_i_stream->SeekI(-(off_t)m_i_streambuf->GetBytesLeft(),
                             wxFromCurrent);

    delete m_i_streambuf;
}

char wxBufferedInputStream::Peek()
{
    return m_i_streambuf->Peek();
}

wxInputStream& wxBufferedInputStream::Read(void *buf, size_t size)
{
    // reset the error flag
    m_lasterror = wxStream_NOERROR;

    // first read from the already cached data
    m_lastcount = GetWBack(buf, size);

    // do we have to read anything more?
    if ( m_lastcount < size )
    {
        size -= m_lastcount;
        buf = (char *)buf + m_lastcount;

        // the call to wxStreamBuffer::Read() below will reset our m_lastcount,
        // so save it
        size_t countOld = m_lastcount;

        m_i_streambuf->Read(buf, size);

        m_lastcount += countOld;
    }

    return *this;
}

off_t wxBufferedInputStream::SeekI(off_t pos, wxSeekMode mode)
{
    // RR: Look at wxInputStream for comments.

    if (m_lasterror==wxSTREAM_EOF)
        m_lasterror=wxSTREAM_NOERROR;

    if (m_wback)
    {
        wxLogDebug( wxT("Seeking in stream which has data written back to it.") );
        
        free(m_wback);
        m_wback = NULL;
        m_wbacksize = 0;
        m_wbackcur = 0;
    }
    
    return m_i_streambuf->Seek(pos, mode);
}

off_t wxBufferedInputStream::TellI() const
{
    off_t pos = m_i_streambuf->Tell();

    if (pos != wxInvalidOffset)
        pos -= (m_wbacksize - m_wbackcur);
        
    return pos;
}

size_t wxBufferedInputStream::OnSysRead(void *buffer, size_t bufsize)
{
    return m_parent_i_stream->Read(buffer, bufsize).LastRead();
}

off_t wxBufferedInputStream::OnSysSeek(off_t seek, wxSeekMode mode)
{
    return m_parent_i_stream->SeekI(seek, mode);
}

off_t wxBufferedInputStream::OnSysTell() const
{
    return m_parent_i_stream->TellI();
}

void wxBufferedInputStream::SetInputStreamBuffer(wxStreamBuffer *buffer)
{
    wxCHECK_RET( buffer, _T("wxBufferedInputStream needs buffer") );

    delete m_i_streambuf;
    m_i_streambuf = buffer;
}

// ----------------------------------------------------------------------------
// wxBufferedOutputStream
// ----------------------------------------------------------------------------

wxBufferedOutputStream::wxBufferedOutputStream(wxOutputStream& s,
                                               wxStreamBuffer *buffer)
                      : wxFilterOutputStream(s)
{
    if ( buffer )
    {
        m_o_streambuf = buffer;
    }
    else // create a default one
    {
        m_o_streambuf = new wxStreamBuffer(*this, wxStreamBuffer::write);

        m_o_streambuf->SetBufferIO(1024);
    }
}

wxBufferedOutputStream::~wxBufferedOutputStream()
{
    Sync();
    delete m_o_streambuf;
}

wxOutputStream& wxBufferedOutputStream::Write(const void *buffer, size_t size)
{
    m_lastcount = 0;
    m_o_streambuf->Write(buffer, size);
    return *this;
}

off_t wxBufferedOutputStream::SeekO(off_t pos, wxSeekMode mode)
{
    Sync();
    return m_o_streambuf->Seek(pos, mode);
}

off_t wxBufferedOutputStream::TellO() const
{
    return m_o_streambuf->Tell();
}

void wxBufferedOutputStream::Sync()
{
    m_o_streambuf->FlushBuffer();
    m_parent_o_stream->Sync();
}

size_t wxBufferedOutputStream::OnSysWrite(const void *buffer, size_t bufsize)
{
    return m_parent_o_stream->Write(buffer, bufsize).LastWrite();
}

off_t wxBufferedOutputStream::OnSysSeek(off_t seek, wxSeekMode mode)
{
    return m_parent_o_stream->SeekO(seek, mode);
}

off_t wxBufferedOutputStream::OnSysTell() const
{
    return m_parent_o_stream->TellO();
}

size_t wxBufferedOutputStream::GetSize() const
{
   return m_parent_o_stream->GetSize() + m_o_streambuf->GetIntPosition();
}

void wxBufferedOutputStream::SetOutputStreamBuffer(wxStreamBuffer *buffer)
{
    wxCHECK_RET( buffer, _T("wxBufferedOutputStream needs buffer") );

    delete m_o_streambuf;
    m_o_streambuf = buffer;
}

// ----------------------------------------------------------------------------
// Some IOManip function
// ----------------------------------------------------------------------------

wxOutputStream& wxEndL(wxOutputStream& stream)
{
    static const wxChar *eol = wxTextFile::GetEOL();

    return stream.Write(eol, wxStrlen(eol));
}

#endif
  // wxUSE_STREAMS
