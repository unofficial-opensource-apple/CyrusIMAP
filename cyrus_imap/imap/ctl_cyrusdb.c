/* ctl_cyrusdb.c -- Program to perform operations common to all cyrus DBs
 *
 * Copyright (c) 1998-2003 Carnegie Mellon University.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The name "Carnegie Mellon University" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For permission or any other legal
 *    details, please contact  
 *      Office of Technology Transfer
 *      Carnegie Mellon University
 *      5000 Forbes Avenue
 *      Pittsburgh, PA  15213-3890
 *      (412) 268-4387, fax: (412) 268-7395
 *      tech-transfer@andrew.cmu.edu
 *
 * 4. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by Computing Services
 *     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
 *
 * CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
 * FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id: ctl_cyrusdb.c,v 1.28 2006/11/30 17:11:17 murch Exp $
 */

#include <config.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <errno.h>

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

#include "annotate.h"
#include "cyrusdb.h"
#include "duplicate.h"
#include "global.h"
#include "exitcodes.h"
#include "libcyr_cfg.h"
#include "mboxlist.h"
#include "seen.h"
#include "tls.h"
#include "util.h"
#include "xmalloc.h"

#define N(a) (sizeof(a) / sizeof(a[0]))

/* config.c stuff */
const int config_need_data = 0;

struct cyrusdb {
    const char *name;
    struct cyrusdb_backend **env;
    int archive;
} dblist[] = {
    { FNAME_MBOXLIST,		&config_mboxlist_db,	1 },
    { FNAME_QUOTADB,		&config_quota_db,	1 },
    { FNAME_ANNOTATIONS,	&config_annotation_db,	1 },
    { FNAME_DELIVERDB,		&config_duplicate_db,	0 },
    { FNAME_TLSSESSIONS,	&config_tlscache_db,	0 },
    { FNAME_PTSDB,              &config_ptscache_db,    0 },
    { NULL,			NULL,			0 }
};

static int compdb(const void *v1, const void *v2)
{
    struct cyrusdb *db1 = (struct cyrusdb *) v1;
    struct cyrusdb *db2 = (struct cyrusdb *) v2;

    return (db1->env - db2->env);
}

void usage(void)
{
    fprintf(stderr, "ctl_cyrusdb [-C <altconfig>] -c\n");
    fprintf(stderr, "ctl_cyrusdb [-C <altconfig>] -r [-x]\n");
    exit(-1);
}

/* Callback for use by recover_reserved */
static int fixmbox(char *name,
		   int matchlen __attribute__((unused)),
		   int maycreate __attribute__((unused)),
		   void *rock __attribute__((unused)))
{
    int mbtype;
    int r;

    /* Do an mboxlist_detail on the mailbox */
    r = mboxlist_detail(name, &mbtype, NULL, NULL, NULL, NULL, NULL);

    /* if it is MBTYPE_RESERVED, unset it & call mboxlist_delete */
    if(!r && (mbtype & MBTYPE_RESERVE)) {
	if(!r) {
	    r = mboxlist_deletemailbox(name, 1, NULL, NULL, 0, 0, 1);
	    if(r) {
		/* log the error */
		syslog(LOG_ERR,
		       "could not remove reserved mailbox '%s': %s",
		       name, error_message(r));
	    } else {
		syslog(LOG_ERR,
		       "removed reserved mailbox '%s'",
		       name);
	    }
	}
    }

    return 0;
}
void recover_reserved() 
{
    char pattern[2] = { '*', '\0' };
    
    mboxlist_init(0);
    mboxlist_open(NULL);

    /* Need annotations.db for mboxlist_deletemailbox() */
    annotatemore_init(0, NULL, NULL);
    annotatemore_open(NULL);

    /* build a list of mailboxes - we're using internal names here */
    mboxlist_findall(NULL, pattern, 1, NULL,
		     NULL, fixmbox, NULL);

    annotatemore_close();
    annotatemore_done();

    mboxlist_close();
    mboxlist_done();
}


int main(int argc, char *argv[])
{
    extern char *optarg;
    int opt, r, r2;
    char *alt_config = NULL;
    int reserve_flag = 1;
    enum { RECOVER, CHECKPOINT, NONE } op = NONE;
    char dirname[1024], backup1[1024], backup2[1024];
    char *archive_files[N(dblist)];
    char *msg = "";
    int i, j, rotated = 0;

    if (geteuid() == 0) fatal("must run as the Cyrus user", EC_USAGE);
    r = r2 = 0;

    while ((opt = getopt(argc, argv, "C:rxc")) != EOF) {
	switch (opt) {
	case 'C': /* alt config file */
	    alt_config = optarg;
	    break;

	case 'r':
	    libcyrus_config_setint(CYRUSOPT_DB_INIT_FLAGS, CYRUSDB_RECOVER);
#ifdef APPLE_OS_X_SERVER
	    msg = "verifying cyrus databases";
#else
	    msg = "recovering cyrus databases";
#endif
	    if (op == NONE) op = RECOVER;
	    else usage();
	    break;

	case 'c':
	    msg = "checkpointing cyrus databases";
	    if (op == NONE) op = CHECKPOINT;
	    else usage();
	    break;

	case 'x':
	    reserve_flag = 0;
	    break;

	default:
	    usage();
	    break;
	}
    }

    if (op == NONE || (op != RECOVER && !reserve_flag)) {
	usage();
	/* NOTREACHED */
    }

    cyrus_init(alt_config, "ctl_cyrusdb", 0);

    /* create the name of the db directory */
    /* (used by backup directory names) */
    strcpy(dirname, config_dir);
    strcat(dirname, FNAME_DBDIR);

    /* create the names of the backup directories */
    strcpy(backup1, dirname);
    strcat(backup1, ".backup1");
    strcpy(backup2, dirname);
    strcat(backup2, ".backup2");

    syslog(LOG_NOTICE, "%s", msg);

    /* sort dbenvs */
    qsort(dblist, N(dblist)-1, sizeof(struct cyrusdb), &compdb);

    memset(archive_files, 0, N(dblist) * sizeof(char*));
    for (i = 0, j = 0; dblist[i].name != NULL; i++) {

	/* if we need to archive this db, add it to the list */
	if (dblist[i].archive) {
	    archive_files[j] = (char*) xmalloc(strlen(config_dir) +
					       strlen(dblist[i].name) + 1);
	    strcpy(archive_files[j], config_dir);
	    strcat(archive_files[j++], dblist[i].name);
	}

	/* deal with each dbenv once */
	if (dblist[i].env == dblist[i+1].env) continue;

	r = r2 = 0;
	switch (op) {
	case RECOVER:
	    break;
	    
	case CHECKPOINT:
	    r2 = (*(dblist[i].env))->sync();
	    if (r2) {
		syslog(LOG_ERR, "DBERROR: sync %s: %s", dirname,
		       cyrusdb_strerror(r2));
		fprintf(stderr, 
			"ctl_cyrusdb: unable to sync environment\n");
	    }

	    /* ARCHIVE */
	    r2 = 0;

	    if (!rotated) {
		/* rotate the backup directories -- ONE time only */
		char *tail;
		DIR *dirp;
		struct dirent *dirent;

		tail = backup2 + strlen(backup2);

		/* remove db.backup2 */
		dirp = opendir(backup2);
		strcat(tail++, "/");

		if (dirp) {
		    while ((dirent = readdir(dirp)) != NULL) {
			if (dirent->d_name[0] == '.') continue;

			strcpy(tail, dirent->d_name);
			unlink(backup2);
		    }

		    closedir(dirp);
		}
		tail[-1] = '\0';
		r2 = rmdir(backup2);

		/* move db.backup1 to db.backup2 */
		if (r2 == 0 || errno == ENOENT)
		    r2 = rename(backup1, backup2);

		/* make a new db.backup1 */
		if (r2 == 0 || errno == ENOENT)
		    r2 = mkdir(backup1, 0755);

		rotated = 1;
	    }

	    /* do the archive */
	    if (r2 == 0)
		r2 = (*(dblist[i].env))->archive((const char**) archive_files,
						 backup1);

	    if (r2) {
		syslog(LOG_ERR, "DBERROR: archive %s: %s", dirname,
		       cyrusdb_strerror(r2));
		fprintf(stderr, 
			"ctl_cyrusdb: unable to archive environment\n");
	    }

	    break;
	    
	default:
	    break;
	}

	/* free the archive_list */
	while (j > 0) {
	    free(archive_files[--j]);
	    archive_files[j] = NULL;
	}
    }

    if(op == RECOVER && reserve_flag)
	recover_reserved();

    cyrus_done();

    syslog(LOG_NOTICE, "done %s", msg);
    exit(r || r2);
}
