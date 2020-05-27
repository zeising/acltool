/*
 * gacl.c - Generic ACLs - Emulate FreeBSD acl* functionality
 *
 * Copyright (c) 2020, Peter Eriksson <pen@lysator.liu.se>
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "strings.h"


#define GACL_C_INTERNAL 1
#include "gacl.h"
#include "gacl_impl.h"

#include "vfs.h"


/* ----- OS-specific stuff below here -------------- */

#ifdef __linux__
#include <arpa/inet.h>
#include <sys/xattr.h>
#include "gacl.h"
#include "nfs4.h"

#define ACL_NFS4_XATTR "system.nfs4_acl"

/*
 * xattr format:
 * 
 * struct acl {
 *   u_int32_t ace_c;
 *   struct ace {
 *     u_int32_t type;
 *     u_int32_t flags;
 *     u_int32_t perms;
 *     struct utf8str_mixed {
 *       u_int32_t len;
 *       char buf[len];
 *     } id;
 *   } ace_v[ace_c];
 * };
 */


static char *
_nfs4_id_domain(void) {
  static char *saved_domain = NULL;
  FILE *fp;
  char buf[256];


  if (saved_domain)
    return saved_domain;

  fp = fopen("/etc/idmapd.conf","r");
  if (!fp)
    return NULL;

  while (fgets(buf, sizeof(buf), fp)) {
    char *bp, *t;
    bp = buf;

    t = strsep(&bp, " \t\n");
    if (!t)
      continue;
    if (strcmp(t, "Domain") == 0) {
      t = strsep(&bp, " \t\n");
      if (!t || strcmp(t, "=") != 0)
	continue;
      t = strsep(&bp, " \t\n");
      if (!t) {
	fclose(fp);
	return NULL;
      }
	
      saved_domain = s_dup(t);
      break;
    }
  }

  fclose(fp);
  return saved_domain;
}

/* This code is a bit of a hack */
static int
_nfs4_id_to_uid(char *buf,
		size_t bufsize,
		uid_t *uidp) {
  struct passwd *pp;
  int i;
  char *idd = NULL;
  int iddlen = -1;


  if (bufsize < 1)
    return -1;

  for (i = 0; i < bufsize && buf[i] != '@'; i++)
    ;

  if (i < bufsize) {
    buf[i++] = '\0';
    
    if (!idd || (iddlen == bufsize-i && strncmp(idd, buf+i, iddlen) == 0)) {
      pp = getpwnam(buf);
      if (pp) {
	*uidp = pp->pw_uid;
	return 1;
      }
    }
  } else if (sscanf(buf, "%d", uidp) == 1)
    return 1;

  return 0;
}

/* This code is a bit of a hack */
static int
_nfs4_id_to_gid(char *buf,
		size_t bufsize,
		gid_t *gidp) {
  struct group *gp;
  int i;
  char *idd = NULL;
  int iddlen = -1;


  if (bufsize < 1)
    return -1;

  idd = _nfs4_id_domain();
  if (idd)
    iddlen = strlen(idd);

  for (i = 0; i < bufsize && buf[i] != '@'; i++)
    ;

  if (i < bufsize) {
    buf[i++] = '\0';
    
    if (!idd || (iddlen == bufsize-i && strncmp(idd, buf+i, iddlen) == 0)) {
      gp = getgrnam(buf);
      if (gp) {
	*gidp = gp->gr_gid;
	return 1;
      }
    }
  } else if (sscanf(buf, "%d", gidp) == 1)
    return 1;

  return 0;
}


static GACL *
_gacl_init_from_nfs4(const char *buf,
		     size_t bufsize) {
  int i;
  u_int32_t *vp;
  u_int32_t na;
  char *cp;
  GACL *ap;

  
  vp = (u_int32_t *) buf;
  na = ntohl(*vp++);
  
  ap = gacl_init(na);
  if (!ap)
    return NULL;

  ap->type = GACL_TYPE_NFS4;
  
  for (i = 0; i < na; i++) {
    u_int32_t idlen;
    GACL_ENTRY *ep;
    
    if (gacl_create_entry_np(&ap, &ep, i) < 0) {
      gacl_free(ap);
      return NULL;
    }
    
    ep->type =  ntohl(*vp++);
    ep->flags = ntohl(*vp++);
    ep->perms = ntohl(*vp++);
    
    idlen = ntohl(*vp++);
    cp = (char *) vp;

    if (ep->flags & NFS4_ACE_IDENTIFIER_GROUP) {
      if (strncmp(cp, "GROUP@", idlen) == 0) {
	ep->tag.name = s_dup("group@");
	ep->tag.type = GACL_TAG_TYPE_GROUP_OBJ;
	ep->tag.ugid = -1;
      } else {
	ep->tag.ugid = -1;
	ep->tag.name = s_ndup(cp, idlen);
	(void) _nfs4_id_to_gid(cp, idlen, &ep->tag.ugid);
	ep->tag.type = GACL_TAG_TYPE_GROUP;
      }
    } else {
      if (strncmp(cp, "OWNER@", idlen) == 0) {
	ep->tag.name = s_dup("group@");
	ep->tag.type = GACL_TAG_TYPE_USER_OBJ;
	ep->tag.ugid = -1;
      } else if (strncmp(cp, "EVERYONE@", idlen) == 0) {
	ep->tag.name = s_dup("everyone@");
	ep->tag.type = GACL_TAG_TYPE_EVERYONE;
	ep->tag.ugid = -1;
      } else {
	ep->tag.ugid = -1;
	ep->tag.name = s_ndup(cp, idlen);
	ep->tag.type = GACL_TAG_TYPE_USER;
	(void) _nfs4_id_to_uid(cp, idlen, &ep->tag.ugid);
      }
    }


    vp += idlen / sizeof(u_int32_t);
    if (idlen % sizeof(u_int32_t))
      vp++;
  }

  return ap;
}


GACL *
_gacl_get_fd_file(int fd,
		  const char *path,
		  GACL_TYPE type,
		  int flags) {
  char *buf;
  ssize_t bufsize, rc;

  
  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW) {
    
      bufsize = lgetxattr(path, ACL_NFS4_XATTR, NULL, 0);
      if (bufsize < 0)
	return NULL;

      buf = malloc(bufsize);
      if (!buf)
	return NULL;

      rc = lgetxattr(path, ACL_NFS4_XATTR, buf, bufsize);
      if (rc < 0) {
	free(buf);
	return NULL;
      }
    } else {
      
      bufsize = getxattr(path, ACL_NFS4_XATTR, NULL, 0);
      if (bufsize < 0)
	return NULL;
      
      buf = malloc(bufsize);
      if (!buf)
	return NULL;
      
      rc = getxattr(path, ACL_NFS4_XATTR, buf, bufsize);
      if (rc < 0) {
	free(buf);
	return NULL;
      }
      
    }
  } else {
    
    bufsize = fgetxattr(fd, ACL_NFS4_XATTR, NULL, 0);
    if (bufsize < 0)
      return NULL;
    
    buf = malloc(bufsize);
    if (!buf)
      return NULL;
    
    rc = fgetxattr(fd, ACL_NFS4_XATTR, buf, bufsize);
    if (rc < 0) {
      free(buf);
      return NULL;
    }
    
  }

  return _gacl_init_from_nfs4(buf, bufsize);
}



static ssize_t 
_gacl_to_nfs4(GACL *ap, 
	      char *buf, 
	      size_t bufsize) {
  u_int32_t *vp, *endp;
  size_t buflen, vlen;
  int i, rc;


  vp = (u_int32_t *) buf;
  buflen = bufsize/sizeof(u_int32_t);
  endp = vp+buflen;

  /* Number of ACEs */
  *vp++ = htonl(ap->ac);

  for (i = 0; i < ap->ac; i++) {
    char *idname;
    u_int32_t idlen;
    struct passwd *pp;
    struct group *gp;
    char *idd;
    char tbuf[256];
    GACL_ENTRY *ep = &ap->av[i];

    if (vp >= endp) {
      errno = ENOMEM;
      return -1;
    }
    *vp++ = htonl(ep->type);

    if (vp >= endp) {
      errno = ENOMEM;
      return -1;
    }
    *vp++ = htonl(ep->flags | (ep->tag.type == GACL_TAG_TYPE_GROUP ||
			       ep->tag.type == GACL_TAG_TYPE_GROUP_OBJ ? 
			       NFS4_ACE_IDENTIFIER_GROUP : 0));
    
    if (vp >= endp) {
      errno = ENOMEM;
      return -1;
    }
    *vp++ = htonl(ep->perms);

    switch (ep->tag.type) {
    case GACL_TAG_TYPE_USER_OBJ:
      idname = "OWNER@";
      break;
    case GACL_TAG_TYPE_GROUP_OBJ:
      idname = "GROUP@";
      break;
    case GACL_TAG_TYPE_EVERYONE:
      idname = "EVERYONE@";
      break;
    case GACL_TAG_TYPE_USER:
      pp = getpwuid(ep->tag.ugid);
      if (pp) {
	idd = _nfs4_id_domain();
	rc = snprintf(tbuf, sizeof(tbuf), "%s@%s", pp->pw_name, idd ? idd : "");
      } else
	rc = snprintf(tbuf, sizeof(tbuf), "%u", ep->tag.ugid);
      if (rc < 0) {
	errno = EINVAL;
	return -1;
      }
      idname = tbuf;
      break;
    case GACL_TAG_TYPE_GROUP:
      gp = getgrgid(ep->tag.ugid);
      if (gp) {
	idd = _nfs4_id_domain();
	rc = snprintf(tbuf, sizeof(tbuf), "%s@%s", gp->gr_name, idd ? idd : "");
      } else
	rc = snprintf(tbuf, sizeof(tbuf), "%u", ep->tag.ugid);
      if (rc < 0) {
	errno = EINVAL;
	return -1;
      }
      idname = tbuf;
      break;
    default:
      errno = EINVAL;
      return -1;
    }

    idlen = strlen(idname);
    vlen = idlen / sizeof(u_int32_t);
    if (idlen % sizeof(u_int32_t))
      ++vlen;

    if (vp+vlen >= endp) {
      errno = ENOMEM;
      return -1;
    }
    *vp++ = htonl(idlen);
    memcpy(vp, idname, idlen);
    vp += vlen;
  }

  return ((char *) vp) - buf;
}


int
_gacl_set_fd_file(int fd,
		  const char *path,
		  GACL_TYPE type,
		  GACL *ap,
		  int flags) {
  char buf[8192];
  ssize_t bufsize, rc;


  bufsize = _gacl_to_nfs4(ap, buf, sizeof(buf));
  if (bufsize < 0)
    return -1;

  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW) {
    
      rc = lsetxattr(path, ACL_NFS4_XATTR, buf, bufsize, 0);
      if (rc < 0)
	return -1;

    } else {
      
      rc = setxattr(path, ACL_NFS4_XATTR, buf, bufsize, 0);
      if (rc < 0)
	return -1;
      
    }
  } else {
    
    rc = fsetxattr(fd, ACL_NFS4_XATTR, buf, bufsize, 0);
    if (rc < 0)
      return -1;
    
  }

  return rc;
}
#endif




/* 
 * ---------- FreeBSD ----------------------------------------
 */
#ifdef __FreeBSD__

/* 
 * First some sanity checks just in case something changes under our feet
 */
#if ACL_USER_OBJ != 0x0001
/* (GACL_TAG_TYPE_USER_OBJ is an enum so can't be checked directly) */
#error ACL Tag Types are incompatible
#endif

#if GACL_PERM_READ_DATA != ACL_READ_DATA
#error ACL Permissions are incompatible
#endif

#if GACL_FLAG_INHERIT_ONLY != ACL_ENTRY_INHERIT_ONLY
#error ACL Flags are incompatible
#endif


/*
 * Convert freebsd acl_entry_t to GACL_ENTRY
 */
static int
_gacl_entry_from_acl_entry(GACL_ENTRY *nep,
			   freebsd_acl_entry_t oep) {
  struct passwd *pp;
  struct group *gp;

  
  nep->tag.type = oep->ae_tag;
  nep->tag.ugid = oep->ae_id;

  switch (nep->tag.type) {
  case GACL_TAG_TYPE_UNKNOWN:
  case GACL_TAG_TYPE_MASK:
  case GACL_TAG_TYPE_OTHER:
    errno = EINVAL;
    return -1;

  case GACL_TAG_TYPE_USER_OBJ:
    nep->tag.name = s_dup("owner@");
    break;
  case GACL_TAG_TYPE_GROUP_OBJ:
    nep->tag.name = s_dup("group@");
    break;
  case GACL_TAG_TYPE_EVERYONE:
    nep->tag.name = s_dup("everyone@");
    break;
    
  case GACL_TAG_TYPE_USER:
    pp = getpwuid(nep->tag.ugid);
    if (pp)
      nep->tag.name = s_dup(pp->pw_name);
    else {
      char buf[256];
      snprintf(buf, sizeof(buf), "%d", nep->tag.ugid);
      nep->tag.name = s_dup(buf);
    }
    break;
    
  case GACL_TAG_TYPE_GROUP:
    gp = getgrgid(nep->tag.ugid);
    if (gp)
      nep->tag.name = s_dup(gp->gr_name);
    else {
      char buf[256];
      snprintf(buf, sizeof(buf), "%d", nep->tag.ugid);
      nep->tag.name = s_dup(buf);
    }
    break;
  }

  /* XXX: Map FreeBSD perms&flags -> GACL perms&flags (currently identical so we take a shortcut) */
  nep->perms = oep->ae_perm;
  nep->flags = oep->ae_flags;

  switch (oep->ae_entry_type) {
  case ACL_ENTRY_TYPE_ALLOW:
    nep->type = GACL_ENTRY_TYPE_ALLOW;
    break;
  case ACL_ENTRY_TYPE_DENY:
    nep->type = GACL_ENTRY_TYPE_DENY;
    break;
  case ACL_ENTRY_TYPE_AUDIT:
    nep->type = GACL_ENTRY_TYPE_AUDIT;
    break;
  case ACL_ENTRY_TYPE_ALARM:
    nep->type = GACL_ENTRY_TYPE_ALARM;
    break;
  default:
    errno = EINVAL;
    return -1;
  }
  
  return 1;
}


/*
 * Convert GACL_ENTRY to FreeBSD acl_entry_t
 */
static int
_acl_entry_from_gace(freebsd_acl_entry_t nep,
		     GACL_ENTRY *oep) {
  if (oep->tag.type == GACL_TAG_TYPE_UNKNOWN ||
      ((oep->tag.type == GACL_TAG_TYPE_USER || oep->tag.type == GACL_TAG_TYPE_GROUP) &&
       oep->tag.ugid == -1)) {
    errno = EINVAL;
    return -1;
  }
    
  /* XXX: Map GACL tags & qualifiers -> FreeBSD tags & qualifiers (currently identical) */
  nep->ae_tag        = oep->tag.type;
  nep->ae_id         = oep->tag.ugid;
  
  /* XXX: Map GACL perms&flags -> FreeBSD perms&flags (currently identical) */
  nep->ae_perm       = oep->perms;
  nep->ae_flags      = oep->flags;

  switch (oep->type) {
  case GACL_ENTRY_TYPE_ALLOW:
    nep->ae_entry_type = ACL_ENTRY_TYPE_ALLOW;
    break;
  case GACL_ENTRY_TYPE_DENY:
    nep->ae_entry_type = ACL_ENTRY_TYPE_DENY;
    break;
  case GACL_ENTRY_TYPE_AUDIT:
    nep->ae_entry_type = ACL_ENTRY_TYPE_AUDIT;
    break;
  case GACL_ENTRY_TYPE_ALARM:
    nep->ae_entry_type = ACL_ENTRY_TYPE_ALARM;
    break;
  default:
    errno = EINVAL;
    return -1;
  }
  
  return 1;
}


/*
 * Read ACL from file
 */
GACL *
_gacl_get_fd_file(int fd,
		  const char *path,
		  GACL_TYPE type,
		  int flags) {
  GACL *nap;
  freebsd_acl_t oap;
  freebsd_acl_entry_t oep;
  int id, rc;
  
  
  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW)
      oap = acl_get_link_np(path, type);
    else
      oap = acl_get_file(path, type);
  } else
    oap = acl_get_fd_np(fd, type);

  nap = gacl_init(0);
  id = ACL_FIRST_ENTRY;
  nap->type = type;
  
  while ((rc = acl_get_entry(oap, id, &oep)) == 1) {
    GACL_ENTRY *nep;

    id = ACL_NEXT_ENTRY;
    if (gacl_create_entry_np(&nap, &nep, -1) < 0)
      goto Fail;

    if (_gacl_entry_from_acl_entry(nep, oep) < 0)
      goto Fail;
  }

  if (rc < 0)
    goto Fail;

  acl_free(oap);
  return nap;

 Fail:
  acl_free(oap);
  gacl_free(nap);
  return NULL;
}


/*
 * Set ACL on file
 */
int
_gacl_set_fd_file(int fd,
		  const char *path,
		  GACL_TYPE type,
		  GACL *ap,
		  int flags) {
  GACL_ENTRY *oep;
  freebsd_acl_t nap;
  int i, rc;
  
  
  nap = acl_init(ap->ac);

  for (i = 0; (rc = gacl_get_entry(ap, i == 0 ? GACL_FIRST_ENTRY : GACL_NEXT_ENTRY, &oep)) == 1; i++) {
    freebsd_acl_entry_t nep;

    if (acl_create_entry_np(&nap, &nep, i) < 0)
      goto Fail;

    if (_acl_entry_from_gace(nep, oep) < 0)
      goto Fail;
  }

  if (rc < 0)
    goto Fail;
  
  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW)
      rc = acl_set_link_np(path, type, nap);
    else
      rc = acl_set_file(path, type, nap);
  } else
    rc = acl_set_fd_np(fd, nap, type);

  acl_free(nap);
  return rc;

 Fail:
  acl_free(ap);
  gacl_free(nap);
  return -1;
}
#endif



/* 
 * ---------- Solaris ----------------------------------------
 */
#ifdef __sun__

/* 
 * Convert GACL_ENTRY to Solaris ace_t
 */
static int
_gacl_entry_to_ace(GACL_ENTRY *ep,
		   ace_t *ap) {
  if (!ep || !ap) {
    errno = EINVAL;
    return -1;
  }

  switch (ep->tag.type) {
  case GACL_TAG_TYPE_USER_OBJ:
    ap->a_flags = ACE_OWNER;
    break;

  case GACL_TAG_TYPE_GROUP_OBJ:
    ap->a_flags = (ACE_GROUP|ACE_IDENTIFIER_GROUP);
    break;

  case GACL_TAG_TYPE_EVERYONE:
    ap->a_flags = ACE_EVERYONE;
    break;

  case GACL_TAG_TYPE_USER:
    ap->a_flags = 0;
    break;

  case GACL_TAG_TYPE_GROUP:
    ap->a_flags = ACE_IDENTIFIER_GROUP;
    break;

  default:
    errno = ENOSYS;
    return -1;
  }
  
  ap->a_who = ep->tag.ugid;

  /* XXX: Convert */
  ap->a_access_mask = ep->perms;
  ap->a_flags |= ep->flags;
  ap->a_type  = ep->type;
  
  return 0;
}


/*
 * Convert Solaris ace_t to GACL_ENTRY
 */
static int
_gacl_entry_from_ace(GACL_ENTRY *ep,
		     ace_t *ap) {
  struct passwd *pp;
  struct group *gp;

  
  if (!ep || !ap) {
    errno = EINVAL;
    return -1;
  }

  switch (ap->a_flags & (ACE_OWNER|ACE_GROUP|ACE_EVERYONE)) {
  case ACE_OWNER:
    ep->tag.type = GACL_TAG_TYPE_USER_OBJ;
    ep->tag.name = s_dup("owner@");
    ep->tag.ugid = -1;
    break;

  case ACE_GROUP:
    ep->tag.type = GACL_TAG_TYPE_GROUP_OBJ;
    ep->tag.name = s_dup("group@");
    ep->tag.ugid = -1;
    break;

  case ACE_EVERYONE:
    ep->tag.type = GACL_TAG_TYPE_EVERYONE;
    ep->tag.name = s_dup("everyone@");
    ep->tag.ugid = -1;
    break;

  default:
    if (ap->a_flags & ACE_IDENTIFIER_GROUP) {
      ep->tag.type = GACL_TAG_TYPE_GROUP;
      ep->tag.ugid = ap->a_who;
      gp = getgrgid(ap->a_who);
      if (gp)
	ep->tag.name = s_dup(gp->gr_name);
      else {
	char buf[256];
	
	snprintf(buf, sizeof(buf), "%d", ap->a_who);
	ep->tag.name = s_dup(buf);
      }
    } else {
      ep->tag.type = GACL_TAG_TYPE_USER;
      ep->tag.ugid = ap->a_who;
      pp = getpwuid(ap->a_who);
      if (pp)
	ep->tag.name = s_dup(pp->pw_name);
      else {
	char buf[256];
	
	snprintf(buf, sizeof(buf), "%d", ap->a_who);
	ep->tag.name = s_dup(buf);
      }
    }
  }

  /* XXX: Convert values */
  ep->perms = ap->a_access_mask;
  ep->flags = (ap->a_flags & GACL_FLAGS_ALL);
  ep->type  = ap->a_type;
  
  return 0;
}



GACL *
_gacl_get_fd_file(int fd,
		  const char *path,
		  GACL_ENTRY_TYPE type,
		  int flags) {
  GACL *ap;
  int i, n;
  ace_t *acp;

  
  if (path && (flags & GACL_F_SYMLINK_NOFOLLOW)) {
    struct stat sb;
    
    if (vfs_lstat(path, &sb) < 0)
      return NULL;
    
    if (S_ISLNK(sb.st_mode)) {
      /* Solaris doesn't support doing ACL operations on symbolic links - sorry */
      errno = ENOSYS;
      return NULL;
    }
  }
  
  switch (type) {
  case GACL_ENTRY_TYPE_NONE:
    return gacl_init(0);

  case GACL_ENTRY_TYPE_NFS4:
    if (path)
      n = acl(path, ACE_GETACLCNT, 0, NULL);
    else
      n = facl(fd, ACE_GETACLCNT, 0, NULL);
      
    if (n < 0)
      return NULL;

    acp = calloc(n, sizeof(*acp));
    if (!acp) {
      return NULL;
    }
    
    ap = gacl_init(n);
    if (!ap) {
      free(acp);
      return NULL;
    }

    ap->type = type;
    ap->ac = n;
    
    if (path)
      n = acl(path, ACE_GETACL, n, (void *) acp);
    else
      n = facl(fd, ACE_GETACL, n, (void *) acp);
    if (n < 0) {
      free(acp);
      gacl_free(ap);
      return NULL;
    }
    
    for (i = 0; i < n; i++)
      if (_gacl_entry_from_ace(&ap->av[i], &acp[i]) < 0) {
	free(acp);
	gacl_free(ap);
	return NULL;
      }
    return ap;

#if 0
  case GACL_ENTRY_TYPE_ACCESS:
  case GACL_ENTRY_TYPE_DEFAULT:
    if (path)
      n = acl(path, GETACLCNT, 0, NULL);
    else
      n = facl(fd, GETACLCNT, 0, NULL);
    if (n < 0)
      return NULL;

    aep = calloc(n, sizeof(*aep));
    if (!aep)
      return NULL;
    
    ap = gacl_init(n);
    if (!ap) {
      free(aep);
      return NULL;
    }

    ap->type = type;
    ap->ac = n;
    
    if (path)
      n = acl(path, GETACL, n, (void *) aep);
    else
      n = facl(fd, GETACL, n, (void *) aep);
    if (n < 0) {
      gacl_free(ap);
      return NULL;
    }
    for (i = 0; i < n; i++)
      if (_gacl_entry_from_aclent(&ap->av[i], &aep[i]) < 0) {
	free(acp);
	gacl_free(ap);
	return NULL;
      }
    ap->type = type;
    ap->ac = cnt;
    return ap;
#endif
    
  default:
    errno = ENOSYS;
    return NULL;
  }

  errno = EINVAL;
  return NULL;
}



int
_gacl_set_fd_file(int fd,
		  const char *path,
		  GACL_ENTRY_TYPE type,
		  GACL *ap,
		  int flags) {
  int i, rc;
  ace_t *acp;
  
  if (path && (flags & GACL_F_SYMLINK_NOFOLLOW)) {
    struct stat sb;
    
    if (vfs_lstat(path, &sb) < 0)
      return -1;
    
    if (S_ISLNK(sb.st_mode)) {
      /* Solaris doesn't support doing ACL operations on symbolic links - sorry */
      errno = ENOSYS;
      return -1;
    }
  }
  
  switch (type) {
  case GACL_ENTRY_TYPE_NONE:
    /* XXX: Remove ACL? */
    errno = ENOSYS;
    return -1;
    
  case GACL_ENTRY_TYPE_NFS4:
    acp = calloc(ap->ac, sizeof(*acp));
    if (!acp)
      return -1;

    for (i = 0; i < ap->ac; i++)
      if (_gacl_entry_to_ace(&ap->av[i], &acp[i]) < 0) {
	free(acp);
	return -1;
      }
    
    if (path)
      rc = acl(path, ACE_SETACL, ap->ac, (void *) acp);
    else
      rc = facl(fd, ACE_SETACL, ap->ac, (void *) acp);
    free(acp);
    return rc;

#if 0
  case GACL_ENTRY_TYPE_ACCESS:
    /* XXX: Do special handling? (R-M-W - only update access parts) */
    if (acl(path, SETACL, ap->ac, (void *) &ap->av[0]) < 0)
      return -1;
    return 0;
    
  case GACL_ENTRY_TYPE_DEFAULT:
    /* XXX: Do special handling? (R-M-W - only update default parts) */
    if (acl(path, SETACL, ap->ac, (void *) &ap->av[0]) < 0)
      return -1;
    return 0;
#endif

  default:
    errno = EINVAL;
    return -1;
  }
}

#endif

#ifdef __APPLE__

#include <membership.h>

struct perm_mapping {
  macos_acl_perm_t macos;
  GACL_PERM gacl;
} pmap[] =
  {
   { __DARWIN_ACL_READ_DATA,           GACL_PERM_READ_DATA },
   { __DARWIN_ACL_WRITE_DATA,          GACL_PERM_WRITE_DATA },
   { __DARWIN_ACL_EXECUTE,             GACL_PERM_EXECUTE },
   { __DARWIN_ACL_DELETE,              GACL_PERM_DELETE },
   { __DARWIN_ACL_APPEND_DATA,         GACL_PERM_APPEND_DATA },
   { __DARWIN_ACL_DELETE_CHILD,        GACL_PERM_DELETE_CHILD },
   { __DARWIN_ACL_READ_ATTRIBUTES,     GACL_PERM_READ_ATTRIBUTES },
   { __DARWIN_ACL_WRITE_ATTRIBUTES,    GACL_PERM_WRITE_ATTRIBUTES },
   { __DARWIN_ACL_READ_EXTATTRIBUTES,  GACL_PERM_READ_NAMED_ATTRS },
   { __DARWIN_ACL_WRITE_EXTATTRIBUTES, GACL_PERM_WRITE_NAMED_ATTRS },
   { __DARWIN_ACL_READ_SECURITY,       GACL_PERM_READ_ACL },
   { __DARWIN_ACL_WRITE_SECURITY,      GACL_PERM_WRITE_ACL },
   { __DARWIN_ACL_CHANGE_OWNER,        GACL_PERM_WRITE_OWNER },
   { __DARWIN_ACL_SYNCHRONIZE,         GACL_PERM_SYNCHRONIZE },
   { -1, -1 },
  };

struct flag_mapping {
  macos_acl_flag_t macos;
  GACL_FLAG gacl;
} fmap[] =
  {
   { __DARWIN_ACL_FLAG_NO_INHERIT,         GACL_FLAG_NO_PROPAGATE_INHERIT },
   { __DARWIN_ACL_ENTRY_INHERITED,         GACL_FLAG_INHERITED },
   { __DARWIN_ACL_ENTRY_FILE_INHERIT,      GACL_FLAG_FILE_INHERIT },
   { __DARWIN_ACL_ENTRY_DIRECTORY_INHERIT, GACL_FLAG_DIRECTORY_INHERIT },
   { __DARWIN_ACL_ENTRY_ONLY_INHERIT,      GACL_FLAG_INHERIT_ONLY },
   { -1, -1 },
  };


static int
_gacl_entry_from_acl_entry(GACL_ENTRY *nep,
			   macos_acl_entry_t oep) {
  int ugtype, i;
  guid_t *guidp;
  macos_acl_tag_t at;
  macos_acl_permset_t ops;
  macos_acl_flagset_t ofs;
  struct passwd *pp;
  struct group *gp;
  char buf[256];

  
  if (acl_get_tag_type(oep, &at) < 0)
    return -1;
  
  switch (at) {
  case ACL_UNDEFINED_TAG:
    return -1;

  case ACL_EXTENDED_ALLOW:
  case ACL_EXTENDED_DENY:
    guidp = acl_get_qualifier(oep);
    nep->type = (at == ACL_EXTENDED_ALLOW ? GACL_ENTRY_TYPE_ALLOW : GACL_ENTRY_TYPE_DENY);

    if (mbr_uuid_to_id((const unsigned char *) guidp, &nep->tag.ugid, &ugtype) < 0)
      return -1;

    switch (ugtype) {
    case ID_TYPE_UID:
      nep->tag.type = GACL_TAG_TYPE_USER;
      pp = getpwuid(nep->tag.ugid);
      if (pp)
	nep->tag.name = s_dup(pp->pw_name);
      else {
	snprintf(buf, sizeof(buf), "%d", nep->tag.ugid);
	nep->tag.name = s_dup(buf);
      }
      break;
      
    case ID_TYPE_GID:
      nep->tag.type = GACL_TAG_TYPE_GROUP;
      gp = getgrgid(nep->tag.ugid);
      if (gp)
	nep->tag.name = s_dup(gp->gr_name);
      else {
	snprintf(buf, sizeof(buf), "%d", nep->tag.ugid);
	nep->tag.name = s_dup(buf);
      }
      break;

    default:
      free(guidp);
      errno = ENOSYS;
      return -1;
    } 
    free(guidp);
 }

  if (acl_get_permset(oep, &ops) < 0)
    return -1;
  
  gacl_clear_perms(&nep->perms);

  for (i = 0; pmap[i].macos != -1; i++) {
    if (acl_get_perm_np(ops, pmap[i].macos))
      gacl_add_perm(&nep->perms, pmap[i].gacl);
  }
  
  if (acl_get_flagset_np(oep, &ofs) < 0)
    return -1;

  gacl_clear_flags_np(&nep->flags);
  
  for (i = 0; fmap[i].macos != -1; i++) {
    if (acl_get_flag_np(ofs, fmap[i].macos))
      gacl_add_flag_np(&nep->flags, fmap[i].gacl);
  }

  return 1;
}

static int
_acl_entry_from_gace(macos_acl_entry_t nep,
		     GACL_ENTRY *oep) {
  GACL_TAG_TYPE etag;
  GACL_ENTRY_TYPE etype;
  GACL_PERMSET *ops;
  GACL_FLAGSET *ofs;
  guid_t guid;
  macos_acl_permset_t perms;
  macos_acl_flagset_t flags;
  int i;
  
  
  if (gacl_get_entry_type_np(oep, &etype) < 0)
    return -1;

  switch (etype) {
  case GACL_ENTRY_TYPE_ALLOW:
    if (acl_set_tag_type(nep, ACL_EXTENDED_ALLOW) < 0)
      return -1;
    break;
  case GACL_ENTRY_TYPE_DENY:
    if (acl_set_tag_type(nep, ACL_EXTENDED_DENY) < 0)
      return -1;
    break;
  default:
    errno = EINVAL;
    return -1;
  }
    
  if (gacl_get_tag_type(oep, &etag) < 0)
    return -1;

  switch (etag) {
  case GACL_TAG_TYPE_USER:
    if (mbr_uid_to_uuid(oep->tag.ugid, (unsigned char *) &guid) < 0)
      return -1;
    break;
  case GACL_TAG_TYPE_GROUP:
    if (mbr_gid_to_uuid(oep->tag.ugid, (unsigned char *) &guid) < 0)
      return -1;
    break;
  default:
    errno = EINVAL;
    return -1;
  }
  
  if (acl_set_qualifier(nep, &guid) < 0)
    return -1;

  
  if (gacl_get_permset(oep, &ops) < 0)
    return -1;

  if (acl_get_permset(nep, &perms) < 0)
    return -1;
  
  acl_clear_perms(perms);

  for (i = 0; pmap[i].macos != -1; i++) {
    if (gacl_get_perm_np(ops, pmap[i].gacl))
      acl_add_perm(perms, pmap[i].macos);
  }
  
  if (acl_set_permset(nep, perms) < 0)
    return -1;
  

  if (gacl_get_flagset_np(oep, &ofs) < 0)
    return -1;

  if (acl_get_flagset_np(nep, &flags) < 0)
    return -1;

  acl_clear_flags_np(flags);
  
  for (i = 0; fmap[i].macos != -1; i++) {
    if (gacl_get_flag_np(ofs, fmap[i].gacl))
      acl_add_flag_np(flags, fmap[i].macos);
  }

  if (acl_set_flagset_np(nep, flags) < 0)
    return -1;
  
  return 1;
}

GACL *
_gacl_get_fd_file(int fd,
		  const char *path,
		  GACL_ENTRY_TYPE type,
		  int flags) {
  GACL *nap;
  macos_acl_t oap;
  macos_acl_entry_t oep;
  int id, rc;
  macos_acl_type_t at = ACL_TYPE_EXTENDED;


  if (type != GACL_ENTRY_TYPE_NFS4) {
    errno = ENOSYS;
    return NULL;
  }

  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW)
      oap = acl_get_link_np(path, at);
    else
      oap = acl_get_file(path, at);
  } else
    oap = acl_get_fd_np(fd, at);

  nap = gacl_init(0);
  id = ACL_FIRST_ENTRY;
  nap->type = type;

  while ((rc = acl_get_entry(oap, id, &oep)) >= 0) {
    GACL_ENTRY *nep;

    id = ACL_NEXT_ENTRY;
    if (gacl_create_entry_np(&nap, &nep, -1) < 0)
      goto Fail;

    if (_gacl_entry_from_acl_entry(nep, oep) < 0)
      goto Fail;
  }

  if (rc < 0 && errno != EINVAL)
    goto Fail;
  
  return nap;

 Fail:
  acl_free(oap);
  gacl_free(nap);
  return NULL;
}


int
_gacl_set_fd_file(int fd,
		  const char *path,
		  GACL_ENTRY_TYPE type,
		  GACL *ap,
		  int flags) {
  GACL_ENTRY *oep;
  macos_acl_t nap;
  int i, rc;
  macos_acl_type_t at = ACL_TYPE_EXTENDED;

  if (type != GACL_ENTRY_TYPE_NFS4) {
    errno = EINVAL;
    return -1;
  }
    
  nap = acl_init(ap->ac);
  if (!nap) {
    fprintf(stderr, "acl_init failed\n");
    return -1;
  }

  for (i = 0; (rc = gacl_get_entry(ap, i == 0 ? GACL_FIRST_ENTRY : GACL_NEXT_ENTRY, &oep)) == 1; i++) {
    macos_acl_entry_t nep;

    if (acl_create_entry_np(&nap, &nep, i) < 0) {
      fprintf(stderr, "acl_create_entry_np failed\n");
      goto Fail;
    }

    if (_acl_entry_from_gace(nep, oep) < 0) {
      fprintf(stderr, "_acl_entry_from_gace failed\n");
      goto Fail;
    }
  }

  if (rc < 0) {
    fprintf(stderr, "gacl_get_entry failed\n");
    goto Fail;
  }
  
  if (path) {
    if (flags & GACL_F_SYMLINK_NOFOLLOW)
      rc = acl_set_link_np(path, at, nap);
    else
      rc = acl_set_file(path, at, nap);
  } else
    rc = acl_set_fd_np(fd, nap, at);

  if (rc < 0)
    fprintf(stderr, "acl_set_xxx failed: rc=%d\n", rc);
  
  acl_free(nap);
  return rc;

 Fail:
  acl_free(ap);
  gacl_free(nap);
  return -1;
}
#endif
