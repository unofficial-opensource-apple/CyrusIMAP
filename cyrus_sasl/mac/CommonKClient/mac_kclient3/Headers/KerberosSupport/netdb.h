/* $Copyright: * * Copyright 1998-2000 by the Massachusetts Institute of Technology. *  * All rights reserved. *  * Permission to use, copy, modify, and distribute this software and its * documentation for any purpose and without fee is hereby granted, * provided that the above copyright notice appear in all copies and that * both that copyright notice and this permission notice appear in * supporting documentation, and that the name of M.I.T. not be used in * advertising or publicity pertaining to distribution of the software * without specific, written prior permission.  Furthermore if you modify * this software you must label your software as modified software and not * distribute it in such a fashion that it might be confused with the * original MIT software. M.I.T. makes no representations about the * suitability of this software for any purpose.  It is provided "as is" * without express or implied warranty. *  * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF * MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE. *  * Individual source code files are copyright MIT, Cygnus Support, * OpenVision, Oracle, Sun Soft, FundsXpress, and others. *  * Project Athena, Athena, Athena MUSE, Discuss, Hesiod, Kerberos, Moira, * and Zephyr are trademarks of the Massachusetts Institute of Technology * (MIT).  No commercial use of these trademarks may be made without prior * written permission of MIT. *  * "Commercial use" means use of a name in a product or other for-profit * manner.  It does NOT prevent a commercial firm from referring to the MIT * trademarks in order to convey information (although in doing so, * recognition of their trademark status should be given). * $ *//* $Header: /cvs/repository/iservers/Servers/cyrus/cyrus_sasl/mac/CommonKClient/mac_kclient3/Headers/KerberosSupport/netdb.h,v 1.1 2004/03/31 18:08:39 dasenbro Exp $ *//*  MIT Sockets Library *  netdb.h *  macdev@mit.edu */#ifndef _NETDB_H#define _NETDB_H#include <stdlib.h>#ifdef __cplusplusextern "C" {#endifstruct hostent{  char  *h_name;        /* official (cannonical) name of host						*/  char **h_aliases;		/* pointer to array of pointers of alias names				*/  int    h_addrtype;	/* host address type: AF_INET or AF_INET6					*/  int    h_length;		/* length of address: 4 or 16								*/  char **h_addr_list;	/* pointer to array of pointers with IPv4 or IPv6 addresses	*/};#define h_addr  h_addr_list[0]	/* first address in list							*/struct servent{  char  *s_name;		/* official service name									*/  char **s_aliases;		/* alias list												*/  int    s_port;		/* port number, network-byte order							*/  char  *s_proto;		/* protocol to use 											*/};#if defined(__CFM68K__) && !defined(__USING_STATIC_LIBS__)#	pragma import on#endif#if !TARGET_RT_MAC_CFM#   pragma d0_pointers on#endifstruct hostent *gethostbyname(const char *hostname);struct hostent *gethostbyaddr(const char *addr, size_t len, int family);/* Gets the local host's hostname.  If namelen isn't long enough, it puts in as much of   the name as possible, without a terminating null.  This is done so it is compatible   with the unix version.  This is, admittedly, the wrong way to write a code, but my   excuse is compatibility.  It should really dynamically allocate space.  Oh well.   It also assert()'s if you don't pass it a reasonably sized buffer. */int gethostname(char *name, size_t namelen);/* Opens the service database if needed and gets the next service entry.   Returns NULL if you have read them all.  On error, returns NULL and   calls SetMITLibError(). */struct servent *getservent (void);/* Closes the service database. On error, calls SetMITLibError(). */void endservent (void);struct servent *getservbyname (const char *servname, const char *protname);struct servent *getservbyport (int port, const char *protname);#if !TARGET_RT_MAC_CFM#   pragma d0_pointers reset#endif#if defined(__CFM68K__) && !defined(__USING_STATIC_LIBS__)#	pragma import reset#endif#ifdef __cplusplus}#endif#endif /* _NETDB_H */