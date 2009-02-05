/* user.c -- User manipulation routines
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
 */
/*
 * $Id: user.c,v 1.21 2006/11/30 17:11:20 murch Exp $
 */

#include <config.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

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

#include "global.h"
#include "user.h"
#include "mboxkey.h"
#include "mboxlist.h"
#include "mailbox.h"
#include "util.h"
#include "seen.h"
#include "quota.h"
#include "xmalloc.h"

#if 0
static int user_deleteacl(char *name, int matchlen, int maycreate, void* rock)
{
    /* deleting all references to the user is too slow right now */

    char *ident = (char *) rock;
    int r;
    char *acl;
    char *rights, *nextid;

    r = mboxlist_lookup(name, &acl, NULL);

    while (!r && acl) {
	rights = strchr(acl, '\t');
	if (!rights) break;
	*rights++ = '\0';

	nextid = strchr(rights, '\t');
	if (!nextid) break;
	*nextid++ = '\0';

	if (!strcmp(acl, ident)) {
	    /* delete ACL for ident */
	    if (!r) mboxlist_setacl(name, ident, (char *)0,
				    1, ident, NULL);
	}

	acl = nextid;
    }
    return 0;
}
#endif

static int user_deletesieve(char *user) 
{
    char hash, *domain;
    char sieve_path[2048];
    char filename[2048];
    DIR *mbdir;
    struct dirent *next = NULL;
    
    /* oh well */
    if(config_getswitch(IMAPOPT_SIEVEUSEHOMEDIR)) return 0;
    
    if (config_virtdomains && (domain = strchr(user, '@'))) {
	char d = (char) dir_hash_c(domain+1);
	*domain = '\0';  /* split user@domain */
	hash = (char) dir_hash_c(user);
	snprintf(sieve_path, sizeof(sieve_path), "%s%s%c/%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR),
		 FNAME_DOMAINDIR, d, domain+1, hash, user);
	*domain = '@';  /* reassemble user@domain */
    }
    else {
	hash = (char) dir_hash_c(user);

	snprintf(sieve_path, sizeof(sieve_path), "%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR), hash, user);
    }

    mbdir=opendir(sieve_path);

    if(mbdir) {
	while((next = readdir(mbdir)) != NULL) {
	    if(!strcmp(next->d_name, ".")
	       || !strcmp(next->d_name, "..")) continue;

	    snprintf(filename, sizeof(filename), "%s/%s",
		     sieve_path, next->d_name);

	    unlink(filename);
	}
	
	closedir(mbdir);

	/* remove mbdir */
	rmdir(sieve_path);
    }

    return 0;
}

int user_deletedata(char *user, char *userid __attribute__((unused)),
		    struct auth_state *authstate __attribute__((unused)),
		    int wipe_user)
{
    char *fname;

    /* delete seen state and mbox keys */
    if(wipe_user) {
	seen_delete_user(user);
	/* XXX  what do we do about multiple backends? */
	mboxkey_delete_user(user);
    }

    /* delete subscriptions */
    fname = mboxlist_hash_usersubs(user);
    (void) unlink(fname);
    free(fname);

    /* delete quotas */
    user_deletequotaroots(user);

    /* delete ACLs - we're using the internal names here */
#if 0
    /* xxx no reason to do this if user_deleteacl is a stub anyway. */
    if(wipe_user) {
	const char pat[] = "*";
	mboxlist_findall(NULL, pat, sizeof(pat), userid,
			 authstate, user_deleteacl,
			 user);
    }
#endif

    /* delete sieve scripts */
    user_deletesieve(user);
    
    return 0;
}

struct rename_rock {
    char *olduser;
    char *newuser;
    char *oldinbox;
    char *newinbox;
    int domainchange;
};

static int user_renamesub(char *name, int matchlen __attribute__((unused)),
			  int maycreate __attribute__((unused)), void* rock)
{
    struct rename_rock *rrock = (struct rename_rock *) rock;
    char newname[MAX_MAILBOX_NAME+1];

    if (!strncasecmp(name, "INBOX", 5) &&
	(name[5] == '\0' || name[5] == '.')) {
	/* generate new name of personal mailbox */
	snprintf(newname, sizeof(newname), "%s%s",
		 rrock->newinbox, name+5);
	name = newname;
    }
    else if (!strncmp(name, rrock->oldinbox, strlen(rrock->oldinbox)) &&
	(name[strlen(rrock->oldinbox)] == '\0' ||
	 name[strlen(rrock->oldinbox)] == '.')) {
	/* generate new name of personal mailbox */
	snprintf(newname, sizeof(newname), "%s%s",
		 rrock->newinbox, name+strlen(rrock->oldinbox));
	name = newname;
    }
    else if (rrock->domainchange) {
	/* if we're changing domains, don't subscribe to other mailboxes */
	return 0;
    }

    mboxlist_changesub(name, rrock->newuser, NULL, 1, 1);

    return 0;
}

static int user_renamesieve(char *olduser, char *newuser)
{
    char hash, *domain;
    char oldpath[2048], newpath[2048];
    int r;
    
    /* oh well */
    if(config_getswitch(IMAPOPT_SIEVEUSEHOMEDIR)) return 0;
    
    if (config_virtdomains && (domain = strchr(olduser, '@'))) {
	char d = (char) dir_hash_c(domain+1);
	*domain = '\0';  /* split user@domain */
	hash = (char) dir_hash_c(olduser);
	snprintf(oldpath, sizeof(oldpath), "%s%s%c/%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR),
		 FNAME_DOMAINDIR, d, domain+1, hash, olduser);
	*domain = '@';  /* reassemble user@domain */
    }
    else {
	hash = (char) dir_hash_c(olduser);

	snprintf(oldpath, sizeof(oldpath), "%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR), hash, olduser);
    }

    if (config_virtdomains && (domain = strchr(newuser, '@'))) {
	char d = (char) dir_hash_c(domain+1);
	*domain = '\0';  /* split user@domain */
	hash = (char) dir_hash_c(newuser);
	snprintf(newpath, sizeof(newpath), "%s%s%c/%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR),
		 FNAME_DOMAINDIR, d, domain+1, hash, newuser);
	*domain = '@';  /* reassemble user@domain */
    }
    else {
	hash = (char) dir_hash_c(newuser);

	snprintf(newpath, sizeof(newpath), "%s/%c/%s",
		 config_getstring(IMAPOPT_SIEVEDIR), hash, newuser);
    }

    /* rename sieve directory
     *
     * XXX this doesn't rename sieve scripts
     */
    r = rename(oldpath, newpath);
    if (r < 0) {
	if (errno == ENOENT) {
	    syslog(LOG_WARNING, "error renaming %s to %s: %m",
		   oldpath, newpath);
	    /* but maybe the user doesn't have any scripts ? */
	    r = 0;
	}
	else if (errno == EXDEV) {
	    syslog(LOG_ERR, "error renaming %s to %s: different filesystems",
		   oldpath, newpath);
	    /* doh!  need to copy entire directory tree */
	}
	else {
	    syslog(LOG_ERR, "error renaming %s to %s: %m", oldpath, newpath);
	}
    }

    return r;
}

int user_renamedata(char *olduser, char *newuser,
		    char *userid __attribute__((unused)),
		    struct auth_state *authstate)
{
    struct namespace namespace;
    char oldinbox[MAX_MAILBOX_NAME+1], newinbox[MAX_MAILBOX_NAME+1];
    char *olddomain, *newdomain;
    struct rename_rock rrock;
    char pat[MAX_MAILBOX_NAME+1];
    int r;

    /* set namespace */
    r = mboxname_init_namespace(&namespace, 0);

    /* get olduser's INBOX */
    if (!r) r = (*namespace.mboxname_tointernal)(&namespace, "INBOX",
						 olduser, oldinbox);

    /* get newuser's INBOX */
    if (!r) r = (*namespace.mboxname_tointernal)(&namespace, "INBOX",
						 newuser, newinbox);

    if (!r) {
	/* copy seen db */
	seen_rename_user(olduser, newuser);
    }

    /* setup rock for find operations */
    rrock.olduser = olduser;
    rrock.newuser = newuser;
    rrock.oldinbox = oldinbox;
    rrock.newinbox = newinbox;

    olddomain = strchr(oldinbox, '!');
    newdomain = strchr(newinbox, '!');
    if ((!olddomain && !newdomain) ||
	(olddomain && newdomain &&
	 (olddomain - oldinbox) == (newdomain - newinbox) &&
	 !strncmp(oldinbox, newinbox, (olddomain - newdomain))))
	rrock.domainchange = 0;
    else
	rrock.domainchange = 1;

    if (!r) {
	/* copy/rename subscriptions - we're using the internal names here */
	strcpy(pat, "*");
	mboxlist_findsub(NULL, pat, 1, olduser, authstate, user_renamesub,
			 &rrock, 1);
    }

    if (!r) {
	/* move sieve scripts */
	user_renamesieve(olduser, newuser);
    }
    
    return r;
}

int user_renameacl(char *name, char *olduser, char *newuser)
{
    int r = 0;
    char *acl;
    char *rights, *nextid;

    r = mboxlist_lookup(name, &acl, NULL);

    while (!r && acl) {
	rights = strchr(acl, '\t');
	if (!rights) break;
	*rights++ = '\0';

	nextid = strchr(rights, '\t');
	if (!nextid) break;
	*nextid++ = '\0';

	if (!strcmp(acl, olduser)) {
	    /* copy ACL for olduser to newuser */
	    r = mboxlist_setacl(name, newuser, rights, 1, newuser, NULL);
	    /* delete ACL for olduser */
	    if (!r)
		r = mboxlist_setacl(name, olduser, (char *)0, 1, newuser, NULL);
	}

	acl = nextid;
    }

    return r;
}

int user_copyquotaroot(char *oldname, char *newname)
{
    int r = 0;
    struct quota quota;

    quota.root = oldname;

    r = quota_read(&quota, NULL, 0);
    if (!r) mboxlist_setquota(newname, quota.limit, 0);

    return r;
}

struct find_rock {
    char *inboxname;
    struct txn **tid;
};

static int find_p(void *rockp,
		  const char *key, int keylen,
		  const char *data __attribute__((unused)),
		  int datalen __attribute__((unused)))
{
    char *inboxname = ((struct find_rock *) rockp)->inboxname;

    return (!strncmp(key, inboxname, strlen(inboxname)) &&
	    (keylen == strlen(inboxname) || key[strlen(inboxname)] == '.'));
}

static int find_cb(void *rockp,
		  const char *key, int keylen,
		  const char *data __attribute__((unused)),
		   int datalen __attribute__((unused)))
{
    struct quota quota_root;
    struct txn **tid = ((struct find_rock *) rockp)->tid;
    int r;

    quota_root.root = (char *) xstrndup(key, keylen);
    r = quota_delete(&quota_root, tid);
    free(quota_root.root); 
    return r;
}

int user_deletequotaroots(const char *user)
{
    struct namespace namespace;
    char buf[MAX_MAILBOX_NAME+1], *inboxname = buf;
    struct txn *tid = NULL;
    int r;

    /* set namespace */
    r = mboxname_init_namespace(&namespace, 0);

    /* get user's toplevel quotaroot (INBOX) */
    if (!r)
	r = (*namespace.mboxname_tointernal)(&namespace, "INBOX",
						 user, inboxname);

    if (!r) {
	struct find_rock frock = { NULL, NULL };
	frock.inboxname = inboxname;
	frock.tid = &tid;
	r = config_quota_db->foreach(qdb, inboxname, strlen(inboxname),
				     &find_p, &find_cb, &frock, &tid);
    }

    if (!r) quota_commit(&tid);

    return r;
}
