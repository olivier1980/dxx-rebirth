/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
/*
 * This code provides a glue layer between PhysicsFS and Simple Directmedia
 *  Layer's (SDL) RWops i/o abstraction.
 *
 * License: this code is public domain. I make no warranty that it is useful,
 *  correct, harmless, or environmentally safe.
 *
 * This particular file may be used however you like, including copying it
 *  verbatim into a closed-source project, exploiting it commercially, and
 *  removing any trace of my name from the source (although I hope you won't
 *  do that). I welcome enhancements and corrections to this file, but I do
 *  not require you to send me patches if you make changes. This code has
 *  NO WARRANTY.
 *
 * Unless otherwise stated, the rest of PhysicsFS falls under the zlib license.
 *  Please see LICENSE in the root of the source tree.
 *
 * SDL falls under the LGPL license. You can get SDL at https://www.libsdl.org/
 *
 *  This file was written by Ryan C. Gordon. (icculus@clutteredmind.org).
 */

#include <stdio.h>  /* used for SEEK_SET, SEEK_CUR, SEEK_END ... */
#include "physfsrwops.h"
#include "physfsx.h"

#define SDL_RWops_callback_seek_position	Sint64
#define SDL_RWops_callback_read_position	size_t
#define SDL_RWops_callback_write_position	size_t


namespace {

static SDL_RWops_callback_seek_position physfsrwops_seek(SDL_RWops *rw, const SDL_RWops_callback_seek_position offset, const int whence)
{
    PHYSFS_File *handle = reinterpret_cast<PHYSFS_File *>(rw->hidden.unknown.data1);
	SDL_RWops_callback_seek_position pos;

    if (whence == SEEK_SET)
    {
        pos = offset;
    } /* if */

    else if (whence == SEEK_CUR)
    {
        PHYSFS_sint64 current = PHYSFS_tell(handle);
        if (current == -1)
        {
            SDL_SetError("Can't find position in file: %s",
                          PHYSFS_getLastError());
            return(-1);
        } /* if */

		if (!std::in_range<SDL_RWops_callback_seek_position>(current))
        {
            SDL_SetError("Can't fit current file position in an int!");
            return(-1);
        } /* if */
        if (offset == 0)  /* this is a "tell" call. We're done. */
			return current;

		/* When `SDL_RWops_callback_seek_position` is `int`, this assignment
		 * converts to a narrower type, but the call to std::in_range above
		 * rejected any values for which the narrowing would change the
		 * observed value.  An assignment which prohibits narrowing would be
		 * ill-formed, since the compile-time check for narrowing is
		 * context-free and assumes the worst case.  Therefore, an
		 * initialization that prohibited narrowing would trigger a compile
		 * error.
		 *
		 * When `SDL_RWops_callback_seek_position` is `Sint64`, then on
		 * x86_64-w64-mingw32,
		 * `static_cast<SDL_RWops_callback_seek_position>(v)` triggers
		 * `-Wuseless-cast`.
		 *
		 * When `SDL_RWops_callback_seek_position` is `Sint64`, then on
		 * x86_64-pc-linux-gnu,
		 * `static_cast<SDL_RWops_callback_seek_position>(v)` is accepted
		 * without issue.
		 */
		const SDL_RWops_callback_seek_position rwcurrent = current;
		pos = rwcurrent + offset;
    } /* else if */

    else if (whence == SEEK_END)
    {
        PHYSFS_sint64 len = PHYSFS_fileLength(handle);
        if (len == -1)
        {
            SDL_SetError("Can't find end of file: %s", PHYSFS_getLastError());
            return(-1);
        } /* if */

		if (!std::in_range<SDL_RWops_callback_seek_position>(len))
        {
            SDL_SetError("Can't fit end-of-file position in an int!");
            return(-1);
        } /* if */

		const SDL_RWops_callback_seek_position rwlen = len;
		pos = rwlen + offset;
    } /* else if */

    else
    {
        SDL_SetError("Invalid 'whence' parameter.");
        return(-1);
    } /* else */

    if ( pos < 0 )
    {
        SDL_SetError("Attempt to seek past start of file.");
        return(-1);
    } /* if */
    
    if (!PHYSFS_seek(handle, static_cast<PHYSFS_uint64>(pos)))
    {
        SDL_SetError("PhysicsFS error: %s", PHYSFS_getLastError());
        return(-1);
    } /* if */

    return(pos);
} /* physfsrwops_seek */


static SDL_RWops_callback_read_position physfsrwops_read(SDL_RWops *const rw, void *const ptr, const SDL_RWops_callback_read_position size, const SDL_RWops_callback_read_position maxnum)
{
    PHYSFS_File *handle = reinterpret_cast<PHYSFS_File *>(rw->hidden.unknown.data1);
	const auto count{size * maxnum};
	const auto rc{PHYSFS_readBytes(handle, ptr, count)};
	if (rc != count)
    {
        if (!PHYSFS_eof(handle)) /* not EOF? Must be an error. */
            SDL_SetError("PhysicsFS error: %s", PHYSFS_getLastError());
    } /* if */
	return rc;
} /* physfsrwops_read */


static SDL_RWops_callback_write_position physfsrwops_write(SDL_RWops *const rw, const void *const ptr, const SDL_RWops_callback_write_position size, const SDL_RWops_callback_write_position num)
{
    PHYSFS_File *handle = reinterpret_cast<PHYSFS_File *>(rw->hidden.unknown.data1);
	const auto count{size * num};
	const auto rc{PHYSFS_writeBytes(handle, reinterpret_cast<const uint8_t *>(ptr), count)};
    if (rc != count)
        SDL_SetError("PhysicsFS error: %s", PHYSFS_getLastError());
	return rc;
} /* physfsrwops_write */


static int physfsrwops_close(SDL_RWops *rw)
{
    PHYSFS_File *handle = reinterpret_cast<PHYSFS_File *>(rw->hidden.unknown.data1);
    if (!PHYSFS_close(handle))
    {
        SDL_SetError("PhysicsFS error: %s", PHYSFS_getLastError());
        return(-1);
    } /* if */

    SDL_FreeRW(rw);
    return(0);
} /* physfsrwops_close */


static std::pair<RWops_ptr, PHYSFS_ErrorCode> create_rwops(RAIIPHYSFS_File handle)
{
    if (!handle)
	{
		const auto err = PHYSFS_getLastErrorCode();
        SDL_SetError("PhysicsFS error: %s", PHYSFS_getErrorByCode(err));
		return {nullptr, err};
	}
    else
    {
		RWops_ptr retval{SDL_AllocRW()};
		if (retval)
        {
            retval->seek  = physfsrwops_seek;
            retval->read  = physfsrwops_read;
            retval->write = physfsrwops_write;
            retval->close = physfsrwops_close;
            retval->hidden.unknown.data1 = handle.release();
        } /* if */
		else
			return {nullptr, PHYSFS_ERR_OTHER_ERROR};
		return {std::move(retval), PHYSFS_ERR_OK};
    } /* else */
} /* create_rwops */

}

std::pair<RWops_ptr, PHYSFS_ErrorCode> PHYSFSRWOPS_openRead(const char *fname)
{
    return(create_rwops(RAIIPHYSFS_File{PHYSFS_openRead(fname)}));
} /* PHYSFSRWOPS_openRead */

std::pair<RWops_ptr, PHYSFS_ErrorCode> PHYSFSRWOPS_openReadBuffered(const char *fname, const PHYSFS_uint64 bufferSize)
{
	RAIIPHYSFS_File fp{PHYSFS_openRead(fname)};
	PHYSFS_setBuffer(fp, bufferSize);
	return create_rwops(std::move(fp));
}

/* end of physfsrwops.c ... */

