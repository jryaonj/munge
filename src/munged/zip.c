/*****************************************************************************
 *  $Id: zip.c,v 1.5 2004/04/03 21:53:00 dun Exp $
 *****************************************************************************
 *  This file is part of the Munge Uid 'N' Gid Emporium (MUNGE).
 *  For details, see <http://www.llnl.gov/linux/munge/>.
 *
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-155910.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License;
 *  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
 *  Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#if HAVE_BZLIB_H
#  include <bzlib.h>
#endif /* HAVE_BZLIB_H */

#if HAVE_ZLIB_H
#  include <zlib.h>
#endif /* HAVE_ZLIB_H */

#include <assert.h>
#include <netinet/in.h>
#include <string.h>
#include <munge.h>
#include "common.h"
#include "zip.h"


/*****************************************************************************
 *  Notes
 *****************************************************************************/
/*
 *  Neither the zlib nor bzlib compression routines encode the original length
 *    of the uncompressed data in the compressed output.
 *  The following "zip" routines allocate an additional 4 bytes (zip_meta_t)
 *    for this purpose.  The original length of the uncompressed data is stored
 *    in the first 4 bytes of the compressed output in MSBF (ie, big endian)
 *    format.
 *  I was tempted to place a sentinel next to this encoded length, but it
 *    didn't seem worth the space, time, or effort.  Modifications to this
 *    length are already detected by the base64 checksum, the cipher checksum
 *    (if encrypted), the compression checksum, and eventually the MAC.
 */


/*****************************************************************************
 *  Data Types
 *****************************************************************************/

typedef unsigned int zip_meta_t;


/*****************************************************************************
 *  Public Functions
 *****************************************************************************/

int
zip_is_valid_type (munge_zip_t type)
{
#if HAVE_PKG_BZLIB
    if (type == MUNGE_ZIP_BZLIB)
        return (1);
#endif /* HAVE_PKG_BZLIB */

#if HAVE_PKG_ZLIB
    if (type == MUNGE_ZIP_ZLIB)
        return (1);
#endif /* HAVE_PKG_ZLIB */

    return (0);
}


int
zip_compress_block (munge_zip_t type,
                    void *dst, int *dstlen, const void *src, int srclen)
{
    unsigned char *xdst;
    unsigned int   xdstlen;
    unsigned char *xsrc;
    unsigned int   xsrclen;
    zip_meta_t     meta;

    assert (type > MUNGE_ZIP_NONE);
    assert (type < MUNGE_ZIP_LAST_ENTRY);
    assert (dst != NULL);
    assert (dstlen != NULL);
    assert (src != NULL);

    if (!zip_is_valid_type (type)) {
        return (-1);
    }
    if (*dstlen < sizeof (meta)) {
        return (-1);
    }
    xdst = (unsigned char *) dst + sizeof (meta);
    xdstlen = *dstlen - sizeof (meta);
    xsrc = (unsigned char *) src;
    xsrclen = srclen;

#if HAVE_PKG_BZLIB
    if (type == MUNGE_ZIP_BZLIB) {
        if (BZ2_bzBuffToBuffCompress ((char *) xdst, &xdstlen,
                (char *) xsrc, xsrclen, 9, 0, 0) != BZ_OK)
            return (-1);
    }
#endif /* HAVE_PKG_BZLIB */

#if HAVE_PKG_ZLIB
    /*
     *  XXX: The use of the "xdstlen_ul" temporary variable is to avoid the
     *       gcc3.3 compiler warning: "dereferencing type-punned pointer
     *       will break strict-aliasing rules".  A mere cast doesn't suffice.
     */
    if (type == MUNGE_ZIP_ZLIB) {
        unsigned long xdstlen_ul = xdstlen;
        if (compress (xdst, &xdstlen_ul,
                xsrc, (unsigned long) xsrclen) != Z_OK)
            return (-1);
        xdstlen = xdstlen_ul;
    }
#endif /* HAVE_PKG_ZLIB */

    *dstlen = xdstlen + sizeof (meta);
    meta = htonl (xsrclen);
    memcpy (dst, &meta, sizeof (meta));
    return (0);
}


int
zip_decompress_block (munge_zip_t type,
                      void *dst, int *dstlen, const void *src, int srclen)
{
    unsigned char *xdst;
    unsigned int   xdstlen;
    unsigned char *xsrc;
    unsigned int   xsrclen;
    zip_meta_t     meta;

    assert (type > MUNGE_ZIP_NONE);
    assert (type < MUNGE_ZIP_LAST_ENTRY);
    assert (dst != NULL);
    assert (dstlen != NULL);
    assert (src != NULL);

    if (!zip_is_valid_type (type)) {
        return (-1);
    }
    memcpy (&meta, src, sizeof (meta));
    meta = ntohl (meta);
    if (*dstlen < (int) meta) {
        return (-1);
    }
    xdst = dst;
    xdstlen = *dstlen;
    xsrc = (unsigned char *) src + sizeof (meta);
    xsrclen = srclen - sizeof (meta);

#if HAVE_PKG_BZLIB
    if (type == MUNGE_ZIP_BZLIB) {
        if (BZ2_bzBuffToBuffDecompress ((char *) xdst, &xdstlen,
                (char *) xsrc, xsrclen, 0, 0) != BZ_OK)
            return (-1);
    }
#endif /* HAVE_PKG_BZLIB */

#if HAVE_PKG_ZLIB
    /*
     *  XXX: The use of the "xdstlen_ul" temporary variable is to avoid the
     *       gcc3.3 compiler warning: "dereferencing type-punned pointer
     *       will break strict-aliasing rules".  A mere cast doesn't suffice.
     */
    if (type == MUNGE_ZIP_ZLIB) {
        unsigned long xdstlen_ul = xdstlen;
        if (uncompress (xdst, &xdstlen_ul,
                xsrc, (unsigned long) xsrclen) != Z_OK)
            return (-1);
        xdstlen = xdstlen_ul;
    }
#endif /* HAVE_PKG_ZLIB */

    *dstlen = xdstlen;
    return (0);
}


int
zip_compress_length (munge_zip_t type, const void *src, int len)
{
/*  For zlib "deflate" compression, allocate an output buffer at least 0.1%
 *    larger than the uncompressed input, plus an additional 12 bytes.
 *  For bzlib compression, allocate an output buffer at least 1% larger than
 *    the uncompressed input, plus an additional 600 bytes.
 *  Also reserve space for encoding the size of the uncompressed data.
 *  The "+1" is for the double-to-int conversion to perform a ceiling function.
 *
 *  XXX: Note the [src] parm is not currently used here.
 */
#if HAVE_PKG_BZLIB
    if (type == MUNGE_ZIP_BZLIB)
        return ((int) ((len * 1.01) + 600 + 1 + sizeof (zip_meta_t)));
#endif /* HAVE_PKG_BZLIB */

#if HAVE_PKG_ZLIB
    if (type == MUNGE_ZIP_ZLIB)
        return ((int) ((len * 1.001) + 12 + 1 + sizeof (zip_meta_t)));
#endif /* HAVE_PKG_ZLIB */

    return (-1);
}


int
zip_decompress_length (munge_zip_t type, const void *src, int len)
{
/*  XXX: Note the [type] parm is not currently used here.
 */
    zip_meta_t     meta;

    assert (src != NULL);

    if (len < sizeof (meta)) {
        return (-1);
    }
    memcpy (&meta, src, sizeof (meta));
    meta = ntohl (meta);
    return ((int) meta);
}


munge_zip_t
zip_select_default_type (munge_zip_t type)
{
/*  Selects an available compression type (assuming compression is requested
 *    by the specified [type]) with a preference towards zlib since it's fast
 *    with low overhead.
 */
    munge_zip_t z;
    munge_zip_t z_def;

    z = MUNGE_ZIP_DEFAULT;
    z_def = MUNGE_ZIP_NONE;

#if HAVE_PKG_BZLIB
    z_def = MUNGE_ZIP_BZLIB;
    if (type == MUNGE_ZIP_BZLIB) {
        z = MUNGE_ZIP_BZLIB;
    }
#endif /* HAVE_PKG_BZLIB */

#if HAVE_PKG_ZLIB
    z_def = MUNGE_ZIP_ZLIB;
    if (type == MUNGE_ZIP_ZLIB) {
        z = MUNGE_ZIP_ZLIB;
    }
#endif /* HAVE_PKG_ZLIB */

    if (type == MUNGE_ZIP_NONE) {
        z = MUNGE_ZIP_NONE;
    }
    else if (z == MUNGE_ZIP_DEFAULT) {
        z = z_def;
    }
    return (z);
}