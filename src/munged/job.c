/*****************************************************************************
 *  $Id$
 *****************************************************************************
 *  This file is part of the Munge Uid 'N' Gid Emporium (MUNGE).
 *  For details, see <http://www.llnl.gov/linux/munge/>.
 *
 *  Copyright (C) 2003-2005 The Regents of the University of California.
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
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <errno.h>
#include <munge.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "conf.h"
#include "dec_v1.h"
#include "enc_v1.h"
#include "log.h"
#include "m_msg.h"
#include "munge_defs.h"
#include "str.h"
#include "work.h"


/*****************************************************************************
 *  Extern Variables
 *****************************************************************************/

extern int done;                        /* defined in munged.c               */


/*****************************************************************************
 *  Private Prototypes
 *****************************************************************************/

static void _job_exec (m_msg_t m);


/*****************************************************************************
 *  Public Functions
 *****************************************************************************/

void
job_accept (conf_t conf)
{
    work_p  w;
    m_msg_t m;
    int     sd;

    assert (conf != NULL);
    assert (conf->ld >= 0);

    if (!(w = work_init ((work_func_t) _job_exec, conf->nthreads))) {
        log_errno (EMUNGE_SNAFU, LOG_ERR,
            "Unable to create %d work thread%s", conf->nthreads,
            ((conf->nthreads > 1) ? "s" : ""));
    }
    log_msg (LOG_INFO, "Created %d work thread%s", conf->nthreads,
            ((conf->nthreads > 1) ? "s" : ""));

    while (!done) {
        if ((sd = accept (conf->ld, NULL, NULL)) < 0) {
            switch (errno) {
                case ECONNABORTED:
                case EINTR:
                    continue;
                case EMFILE:
                case ENFILE:
                case ENOBUFS:
                case ENOMEM:
                    log_msg (LOG_INFO,
                        "Suspended new connections while processing backlog");
                    work_wait (w);
                    continue;
                default:
                    log_errno (EMUNGE_SNAFU, LOG_ERR,
                        "Unable to accept connection");
                    break;
            }
        }
        if (m_msg_create (&m, sd) != EMUNGE_SUCCESS) {
            close (sd);
            log_msg (LOG_WARNING, "Unable to create client request");
        }
        else if (work_queue (w, m) < 0) {
            m_msg_destroy (m);
            log_msg (LOG_WARNING, "Unable to queue client request");
        }
    }
    work_fini (w, 1);
    return;
}


void
job_error (m_msg_t m)
{
/*  If an error condition has been set for the message [m], copy it to the
 *    version-specific message format for transport over the domain socket.
 */
    struct m_msg_v1 *m1;

    assert (m != NULL);

    if (m->errnum != EMUNGE_SUCCESS) {
        m1 = m->pbody;
        m1->error_num = m->errnum;
        m1->error_str =
            strdup (m->errstr ? m->errstr : munge_strerror (m->errnum));
        m1->error_len = strlen (m1->error_str) + 1;
    }
    return;
}


/*****************************************************************************
 *  Private Functions
 *****************************************************************************/

static void
_job_exec (m_msg_t m)
{
/*  Receives and responds to the message request [m].
 */
    munge_err_t  e;
    const char  *p;

    assert (m != NULL);

    if ((e = m_msg_recv (m, MUNGE_MAXIMUM_REQ_LEN)) != EMUNGE_SUCCESS) {
        ; /* fall out of if clause, log error, and drop request */
    }
    else if (m->head.version != MUNGE_MSG_VERSION) {
        m_msg_set_err (m, EMUNGE_SNAFU,
            strdupf ("Invalid message version %d", m->head.version));
    }
    else {
        switch (m->head.type) {
            case MUNGE_MSG_ENC_REQ:
                enc_v1_process_msg (m);
                break;
            case MUNGE_MSG_DEC_REQ:
                dec_v1_process_msg (m);
                break;
            default:
                m_msg_set_err (m, EMUNGE_SNAFU,
                    strdupf ("Invalid message type %d", m->head.type));
                break;
        }
    }
    if (m->errnum != EMUNGE_SUCCESS) {
        p = (m->errstr != NULL) ? m->errstr : munge_strerror (m->errnum);
        log_msg (LOG_INFO, "%s", p);
    }
    m_msg_destroy (m);
    return;
}
