/*
 * misc.c - Misc ACL utility functions
 *
 * Copyright (c) 2019-2020, Peter Eriksson <pen@lysator.liu.se>
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
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "acltool.h"

#define NEW(vp) ((vp) = malloc(sizeof(*(vp))))

/*
 * Calculate the difference between two struct timespec, returns elapsed time i microseconds.
 * Also returns the elapsed time and a unit as a string.
 */
long
ts_delta(struct timespec *x,
	 const struct timespec *y,
	 long *res,
	 char **unit) {
  struct timespec r;
  
  /* Avoid overflow of r.tv_nsec */
  if (x->tv_nsec < y->tv_nsec) {
    x->tv_nsec += 1000000000L;
    x->tv_sec  -= 1;
  }
  
  r.tv_sec  = x->tv_sec - y->tv_sec;
  r.tv_nsec = x->tv_nsec - y->tv_nsec;
  
  if (unit && res) {
    if (r.tv_sec >= 600) {
      /* More than 10 minutes -> return minutes */
      *unit = "m";
      *res = r.tv_sec / 60;
    } else if (r.tv_sec >= 10) {
      /* More than 10 seconds - return seconds */
      *unit = "s";
      *res = r.tv_sec;
    } else if (r.tv_sec == 0) {
      if (r.tv_nsec <= 10000) {
	/* Less than 10us - return nanoseconds */
	*unit = "ns";
	*res = r.tv_nsec;
      } else if (r.tv_nsec <= 10000000) {
	/* Less than 10ms - return microseconds */
	*unit = "µs";
	*res = r.tv_nsec / 1000;
      } else {
	*unit = "ms";
	*res = r.tv_nsec / 1000000;
      }
    } else {
      *unit = "ms";
      *res = r.tv_sec * 1000 + r.tv_nsec / 1000000;
    }
  }
  
  /* Microseconds */
  return r.tv_sec * 1000000 + r.tv_nsec / 1000;
}


static struct gace_perm2c {
  int p;
  char c;
  char *s;
} p2c[] = {
		{ GACE_READ_DATA, 'r', "read_data" },
		{ GACE_WRITE_DATA, 'w', "write_data" },
		{ GACE_EXECUTE, 'x', "execute" },
		{ GACE_APPEND_DATA, 'p', "append_data" },
		{ GACE_DELETE, 'd', "delete" },
		{ GACE_DELETE_CHILD, 'D', "delete_child" },
		{ GACE_READ_ATTRIBUTES, 'a', "read_attributes" },
		{ GACE_WRITE_ATTRIBUTES, 'A', "write_attributes" },
		{ GACE_READ_NAMED_ATTRS, 'R', "read_xattrs" },
		{ GACE_WRITE_NAMED_ATTRS, 'W', "write_xattrs" },
		{ GACE_READ_ACL, 'c', "read_acl" },
		{ GACE_WRITE_ACL, 'C', "write_acl" },
		{ GACE_WRITE_OWNER, 'o', "write_owner" },
		{ GACE_SYNCHRONIZE, 's', "synchronize" },
		{ 0, 0 }
};

static struct gace_flag2c {
  int f;
  char c;
} f2c[] = {
		{ GACE_FLAG_FILE_INHERIT, 'f' },
		{ GACE_FLAG_DIRECTORY_INHERIT, 'd' },
		{ GACE_FLAG_INHERIT_ONLY, 'i' },
		{ GACE_FLAG_NO_PROPAGATE_INHERIT, 'n' },
		{ GACE_FLAG_SUCCESSFUL_ACCESS, 'S' },
		{ GACE_FLAG_FAILED_ACCESS, 'F' },
#ifdef GACE_FLAG_INHERITED
		{ GACE_FLAG_INHERITED, 'I' },
#endif
		{ 0, 0 }
};


static struct perm2c_windows {
  int p;
  char *c;
  char *s;
} p2c_windows[] = {
	   { ACL_READ_DATA,         "R",   "read_data"        },
	   { ACL_WRITE_DATA,        "W",   "write_data"       },
	   { ACL_EXECUTE,           "X",   "execute"          },
	   { ACL_DELETE,            "D",   "delete"           },
	   { ACL_WRITE_ACL,         "P",   "write_acl"        },
	   { ACL_WRITE_OWNER,       "O",   "write_owner"      },
	   { ACL_READ_ATTRIBUTES,   "RA",  "read_attributes"  },
	   { ACL_WRITE_ATTRIBUTES,  "WA",  "write_attributes" },
	   { ACL_DELETE_CHILD,      "DC",  "delete_child"     },
	   { ACL_APPEND_DATA,       "AD",  "append_data"      },
	   { ACL_READ_NAMED_ATTRS,  "REA", "read_xattrs"      },
	   { ACL_WRITE_NAMED_ATTRS, "WEA", "write_xattrs"     },
	   { ACL_SYNCHRONIZE,       "S",   "synchronize"      },
	   { ACL_READ_ACL,          "AS",  "read_acl"         }, 
	   { 0, NULL, NULL }
};


static struct flag2str_windows {
  int f;
  char *s;
} f2c_windows[] = {
	   { ACL_ENTRY_FILE_INHERIT,         "OI" },
	   { ACL_ENTRY_DIRECTORY_INHERIT,    "CI" },
	   { ACL_ENTRY_INHERITED,            "I"  },
	   { ACL_ENTRY_NO_PROPAGATE_INHERIT, "NP" },
	   { ACL_ENTRY_INHERIT_ONLY,         "IO" },
#ifdef ACL_ENTRY_SUCCESSFUL_ACCESS
	   { ACL_ENTRY_SUCCESSFUL_ACCESS,    "S"  },
#endif
#ifdef ACL_ENTRY_FAILED_ACCESS
	   { ACL_ENTRY_FAILED_ACCESS,        "F"  },
#endif
	   { 0, NULL }
};








char *
permset2str(acl_permset_t psp, char *res) {
  static char buf[64];
  int i, a;

  
  if (!res)
    res = buf;
  
  for (i = 0; p2c[i].c; i++) {
    a = acl_get_perm_np(psp, p2c[i].p);
    res[i] = a ? p2c[i].c : '-';
  }
  res[i] = '\0';
  return res;
}

char *
permset2str_samba(acl_permset_t psp,
		  char *res) {
  static char buf[64];
  int i, a, n;

  
  if (!res)
    res = buf;

  res[0] = '\0';
  n = 0;
  for (i = 0; p2c_windows[i].c; i++) {
    a = acl_get_perm_np(psp, p2c_windows[i].p);
    if (a) {
      if (n++)
	strcat(res, "|");
      strcat(res, p2c_windows[i].c);
    }
  }
  return res;
}


char *
permset2str_icacls(acl_permset_t psp,
		   char *res) {
  static char buf[64];
  int i, a, n;

  
  if (!res)
    res = buf;

  res[0] = '\0';
  n = 0;
  strcat(res, "(");
  for (i = 0; p2c_windows[i].c; i++) {
    a = acl_get_perm_np(psp, p2c_windows[i].p);
    if (a) {
      if (n++)
	strcat(res, ",");
      strcat(res, p2c_windows[i].c);
    }
  }
  strcat(res, ")");
  return res;
}

char *
flagset2str(acl_flagset_t fsp, char *res) {
  static char buf[64];
  int i, a;

  
  if (!res)
    res = buf;
  
  for (i = 0; f2c[i].c; i++) {
    a = acl_get_flag_np(fsp, f2c[i].f);
    res[i] = a ? f2c[i].c : '-';
  }
  res[i] = '\0';
  return res;
}

char *
flagset2str_samba(acl_flagset_t fsp,
		  char *res) {
  static char buf[64];
  int i, a, n;

  
  if (!res)
    res = buf;

  res[0] = '\0';

  n = 0;
  for (i = 0; f2c_windows[i].s; i++) {
    a = acl_get_flag_np(fsp, f2c_windows[i].f);
    if (a) {
      if (n++)
	strcat(res, "|");
      strcat(res, f2c_windows[i].s);
    }
  }

  return res;
}

char *
flagset2str_icacls(acl_flagset_t fsp,
		   char *res) {
  static char buf[64];
  int i, a;

  
  if (!res)
    res = buf;

  res[0] = '\0';

  for (i = 0; f2c_windows[i].s; i++) {
    a = acl_get_flag_np(fsp, f2c_windows[i].f);
    if (a) {
      strcat(res, "(");
      strcat(res, f2c_windows[i].s);
      strcat(res, ")");
    }
  }

  return res;
}


const char *
aet2str(const acl_entry_type_t aet) {
  switch (aet) {
  case ACL_ENTRY_TYPE_UNDEFINED:
    return NULL;
    
  case ACL_ENTRY_TYPE_ALLOW:
    return "allow";

  case ACL_ENTRY_TYPE_DENY:
    return "deny";

  case ACL_ENTRY_TYPE_AUDIT:
    return "audit";

  case ACL_ENTRY_TYPE_ALARM:
    return "alarm";
  }

  return NULL;
}



char *
ace2str_samba(acl_entry_t ae,
	      char *rbuf,
	      size_t rsize,
	      const struct stat *sp) {
  static char buf[256];
  char *res;
  acl_tag_t at;
  acl_permset_t aps;
  acl_flagset_t afs;
  acl_entry_type_t aet;
  void *qp = NULL;
  struct passwd *pp = NULL;
  struct group *gp = NULL;
  int rc;
  

  if (!rbuf) {
    rbuf = buf;
    rsize = sizeof(buf);
  }

  res = rbuf;
  
  if (acl_get_tag_type(ae, &at) < 0)
    return NULL;

  switch (at) {
  case ACL_USER:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    pp = getpwuid(*(uid_t *) qp);
    if (pp) {
      gp = getgrnam(pp->pw_name);
      rc = snprintf(res, rsize, "ACL:%s%s:", pp->pw_name, gp ? "(user)" : "");
    } else {
      gp = getgrgid(*(gid_t *) qp);
      rc = snprintf(res, rsize, "ACL:%u%s:", * (uid_t *) qp, gp ? "(user)" : "");
    }
    acl_free(qp);
    break;
    
  case ACL_GROUP:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    gp = getgrgid(*(gid_t *) qp);
    if (gp) { 
      pp = getpwnam(gp->gr_name);
      rc = snprintf(res, rsize, "ACL:%s%s:", gp->gr_name, pp ? "(group)" : "");
    } else {
      pp = getpwuid(*(uid_t *) qp);
      rc = snprintf(res, rsize, "ACL:%u%s:", * (gid_t *) qp, pp ? "(group)" : "");
    }
    acl_free(qp);
    break;
    
  case ACL_USER_OBJ:
    pp = getpwuid(sp->st_uid);
    if (pp)
      rc = snprintf(res, rsize, "ACL:%s:", pp->pw_name);
    else
      rc = snprintf(res, rsize, "ACL:%u:", sp->st_uid);
    break;
    
  case ACL_GROUP_OBJ:
    gp = getgrgid(sp->st_gid);
    if (gp) {
      if (getpwnam(gp->gr_name))
	rc = snprintf(res, rsize, "ACL:GROUP=%s:", gp->gr_name);
      else
	rc = snprintf(res, rsize, "ACL:%s:", gp->gr_name);
    } else
      rc = snprintf(res, rsize, "ACL:GID=%u:", sp->st_gid);
    break;

#ifdef ACL_MASK
  case ACL_MASK:
    rc = snprintf(res, rsize, "ACL:%s:", "mask@");
    break;
#endif

  case ACL_OTHER_OBJ:
    rc = snprintf(res, rsize, "ACL:%s:", "Everyone");
    break;

  case ACL_EVERYONE:
    rc = snprintf(res, rsize, "ACL:%s:", "Everyone");
    break;

  default:
    errno = EINVAL;
    return NULL;
  }
  
  if (rc < 0)
    return NULL;
  
  if (rc > rsize-1) {
    errno = ENOMEM;
    return NULL;
  }
  
  rbuf += rc;
  rsize -= rc;

  
  acl_get_entry_type_np(ae, &aet);
  
  switch (aet) {
  case ACL_ENTRY_TYPE_UNDEFINED:
    return NULL;
    
  case ACL_ENTRY_TYPE_ALLOW:
    rc = snprintf(rbuf, rsize, "ALLOWED/");
    break;
  case ACL_ENTRY_TYPE_DENY:
    rc = snprintf(rbuf, rsize, "ALLOWED/");
    break;
  case ACL_ENTRY_TYPE_AUDIT:
    rc = snprintf(rbuf, rsize, "AUDIT/");
    break;
  case ACL_ENTRY_TYPE_ALARM:
    rc = snprintf(rbuf, rsize, "ALARM/");
    break;
  }

  rbuf += rc;
  rsize -= rc;
  

  if (acl_get_flagset_np(ae, &afs) < 0)
    return NULL;
  
  flagset2str_samba(afs, rbuf);
  rc = strlen(rbuf);
  rbuf += rc;
  rsize -= rc;

  rbuf[0] = '/';
  ++rbuf;
  --rsize;

  if (acl_get_permset(ae, &aps) < 0)
    return NULL;
  
  permset2str_samba(aps, rbuf);
  rc = strlen(rbuf);
  rbuf += rc;
  rsize -= rc;

  rbuf[0] = '\t';
  ++rbuf;
  --rsize;
  
  permset2str(aps, rbuf);
  
  return res;
}


char *
ace2str_icacls(acl_entry_t ae,
	      char *rbuf,
	      size_t rsize,
	      const struct stat *sp) {
  static char buf[256];
  char *res;
  acl_tag_t at;
  acl_permset_t aps;
  acl_flagset_t afs;
#if 0
  acl_entry_type_t aet;
#endif
  void *qp = NULL;
  struct passwd *pp = NULL;
  struct group *gp = NULL;
  int rc;
  

  if (!rbuf) {
    rbuf = buf;
    rsize = sizeof(buf);
  }

  res = rbuf;
  
  if (acl_get_tag_type(ae, &at) < 0)
    return NULL;

  switch (at) {
  case ACL_USER:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    pp = getpwuid(*(uid_t *) qp);
    if (pp)
      rc = snprintf(res, rsize, "%s:", pp->pw_name);
    else
      rc = snprintf(res, rsize, "%u:", * (uid_t *) qp);
    acl_free(qp);
    break;
    
  case ACL_GROUP:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    gp = getgrgid(*(gid_t *) qp);
    if (gp) {
      if (getpwnam(gp->gr_name))
	rc = snprintf(res, rsize, "GROUP=%s:", gp->gr_name);
      else
	rc = snprintf(res, rsize, "%s:", gp->gr_name);
    } else
      rc = snprintf(res, rsize, "GID=%u:", * (gid_t *) qp);
    acl_free(qp);
    break;
    
  case ACL_USER_OBJ:
    pp = getpwuid(sp->st_uid);
    if (pp)
      rc = snprintf(res, rsize, "%s:", pp->pw_name);
    else
      rc = snprintf(res, rsize, "%u:", sp->st_uid);
    break;
    
  case ACL_GROUP_OBJ:
    gp = getgrgid(sp->st_gid);
    if (gp) {
      if (getpwnam(gp->gr_name))
	rc = snprintf(res, rsize, "GROUP=%s:", gp->gr_name);
      else
	rc = snprintf(res, rsize, "%s:", gp->gr_name);
    } else
      rc = snprintf(res, rsize, "GID=%u:", sp->st_gid);
    break;

#ifdef ACL_MASK
  case ACL_MASK:
    rc = snprintf(res, rsize, "%s:", "mask@");
    break;
#endif

  case ACL_OTHER:
    rc = snprintf(res, rsize, "%s:", "Everyone");
    break;

  case ACL_EVERYONE:
    rc = snprintf(res, rsize, "%s:", "Everyone");
    break;

  default:
    errno = EINVAL;
    return NULL;
  }
  
  if (rc < 0)
    return NULL;
  
  if (rc > rsize-1) {
    errno = ENOMEM;
    return NULL;
  }
  
  rbuf += rc;
  rsize -= rc;


#if 0
  /* XXX: How to show DENY ACEs? */
  
  acl_get_entry_type_np(ae, &aet);
  
  switch (aet) {
  case ACL_ENTRY_TYPE_ALLOW:
    break;
  case ACL_ENTRY_TYPE_DENY:
    rc = snprintf(rbuf, rsize, "DENIED/");
    break;
  case ACL_ENTRY_TYPE_AUDIT:
    rc = snprintf(rbuf, rsize, "AUDIT/");
    break;
  case ACL_ENTRY_TYPE_ALARM:
    rc = snprintf(rbuf, rsize, "ALARM/");
    break;
  }

  rbuf += rc;
  rsize -= rc;
#endif

  if (acl_get_flagset_np(ae, &afs) < 0)
    return NULL;
  
  flagset2str_icacls(afs, rbuf);
  rc = strlen(rbuf);
  rbuf += rc;
  rsize -= rc;

  
  if (acl_get_permset(ae, &aps) < 0)
    return NULL;
  
  permset2str_icacls(aps, rbuf);

  return res;
}



char *
ace2str(acl_entry_t ae,
	char *rbuf,
	size_t rsize) {
  static char buf[256];
  char *res;
  acl_tag_t at;
  acl_permset_t aps;
  acl_flagset_t afs;
  acl_entry_type_t aet;
  void *qp = NULL;
  struct passwd *pp = NULL;
  struct group *gp = NULL;
  int rc;
  

  if (!rbuf) {
    rbuf = buf;
    rsize = sizeof(buf);
  }

  res = rbuf;
  
  if (acl_get_tag_type(ae, &at) < 0)
    return NULL;

  switch (at) {
  case ACL_USER:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    pp = getpwuid(*(uid_t *) qp);
    if (pp)
      rc = snprintf(res, rsize, "u:%s", pp->pw_name);
    else
      rc = snprintf(res, rsize, "u:%u", * (uid_t *) qp);
    acl_free(qp);
    break;
    
  case ACL_GROUP:
    qp = acl_get_qualifier(ae);
    if (!qp)
      return NULL;

    gp = getgrgid(*(gid_t *) qp);
    if (gp)
      rc = snprintf(res, rsize, "g:%s", gp->gr_name);
    else
      rc = snprintf(res, rsize, "g:%u", * (gid_t *) qp);
    acl_free(qp);
    break;
    
  case ACL_USER_OBJ:
    rc = snprintf(res, rsize, "%s", "owner@");
    break;
    
  case ACL_GROUP_OBJ:
    rc = snprintf(res, rsize, "%s", "group@");
    break;

  case ACL_MASK:
    rc = snprintf(res, rsize, "%s", "mask@");
    break;

  case ACL_OTHER:
    rc = snprintf(res, rsize, "%s", "other@");
    break;

  case ACL_EVERYONE:
    rc = snprintf(res, rsize, "%s", "everyone@");
    break;

  default:
    errno = EINVAL;
    return NULL;
  }
  
  if (rc < 0)
    return NULL;
  
  if (rc > rsize-1) {
    errno = ENOMEM;
    return NULL;
  }
  
  rbuf += rc;
  rsize -= rc;

  rbuf[0] = ':';
  ++rbuf;
  --rsize;

  if (acl_get_permset(ae, &aps) < 0)
    return NULL;
  
  permset2str(aps, rbuf);
  rc = strlen(rbuf);
  rbuf += rc;
  rsize -= rc;

  rbuf[0] = ':';
  ++rbuf;
  --rsize;
  
  if (acl_get_flagset_np(ae, &afs) < 0)
    return NULL;
  
  flagset2str(afs, rbuf);
  rc = strlen(rbuf);
  rbuf += rc;
  rsize -= rc;

  rbuf[0] = ':';
  ++rbuf;
  --rsize;

  acl_get_entry_type_np(ae, &aet);
  strcpy(rbuf, aet2str(aet));
  return res;
}



typedef struct ftdcb {
  char *path;
  struct stat stat;
  size_t base;
  size_t level;
  struct ftdcb *next;
} FTDCB;

typedef struct ftcb {
  FTDCB *head;
  FTDCB **lastp;
} FTCB;


static void
_ftcb_init(FTCB *ftcb) {
  ftcb->head = NULL;
  ftcb->lastp = &ftcb->head;
}

static void
_ftcb_destroy(FTCB *ftcb) {
  FTDCB *ftdcb, *next;

  for (ftdcb = ftcb->head; ftdcb; ftdcb = next) {
    next = ftdcb->next;
    
    free(ftdcb->path);
    free(ftdcb);
  }
}


int
_ft_foreach(const char *path,
	    struct stat *stat,
	    int (*walker)(const char *path,
			  const struct stat *stat,
			  size_t base,
			  size_t level,
			  void *vp),
	    void *vp,
	    size_t curlevel,
	    size_t maxlevel,
	    mode_t filetypes) {
  FTCB ftcb;
  FTDCB *ftdcb;
  DIR *dp;
  struct dirent *dep;
  int rc;
  struct stat sb;
  size_t plen;

  if (!filetypes || (stat->st_mode & filetypes))
    rc = walker(path, stat, 0, curlevel, vp);
  else
    rc = 0;
  if (rc < 0)
    return rc;

  if (!S_ISDIR(stat->st_mode) || (maxlevel >= 0 && curlevel == maxlevel))
    return 0;

  ++curlevel;
  
  _ftcb_init(&ftcb);
  plen = strlen(path);
  
  dp = opendir(path);
  if (!dp)
    return -1;
  
  while ((dep = readdir(dp)) != NULL) {
    char *fpath;
    
    /* Ignore . and .. */
    if (strcmp(dep->d_name, ".") == 0 ||
	strcmp(dep->d_name, "..") == 0)
      continue;
    
    fpath = s_cat(path, "/", dep->d_name, NULL);
    if (!fpath) {
      rc = -1;
      goto End;
    }

    if (lstat(fpath, &sb) < 0) {
      free(fpath);
      rc = -1;
      goto End;
    }

    /* Add to queue if directory */
    if (S_ISDIR(sb.st_mode)) {
      FTDCB *ftdcb;


      if (NEW(ftdcb) == NULL) {
	rc = -1;
	goto End;
      }
      
      ftdcb->path = fpath;
      ftdcb->base = plen+1;
      ftdcb->stat = sb;
      ftdcb->next = NULL;
      
      *(ftcb.lastp) = ftdcb;
      ftcb.lastp = &ftdcb->next;
    }
    else {
      if (!filetypes || (sb.st_mode & filetypes))
	rc = walker(fpath, &sb, 0, curlevel, vp);
      else
	rc = 0;
      free(fpath);
      if (rc)
	goto End;
    }
  }

  closedir(dp);
  dp = NULL;
  
  for (ftdcb = ftcb.head; ftdcb; ftdcb = ftdcb->next) {
    rc = _ft_foreach(ftdcb->path, &ftdcb->stat, walker, vp, curlevel, maxlevel, filetypes);
    if (rc)
      break;
  }

 End:
  if (dp)
    closedir(dp);
  _ftcb_destroy(&ftcb);
  return rc;
}

int
ft_foreach(const char *path,
	   int (*walker)(const char *path,
			 const struct stat *stat,
			 size_t base,
			 size_t level,
			 void *vp),
	   void *vp,
	   size_t maxlevel,
	   mode_t filetypes) {
  struct stat stat;

  
  if (lstat(path, &stat) < 0)
    return -1;
  
  return _ft_foreach(path, &stat, walker, vp, 0, maxlevel, filetypes);
}


char *
strxcat(const char *str,
	...) {
  va_list ap;
  char *buf, *cp;
  size_t len;
  
  
  if (!str)
    return NULL;
  
  len = strlen(str)+1;
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL)
    len += strlen(cp);
  va_end(ap);
  
  buf = malloc(len);
  if (!buf)
    return NULL;
  
  strcpy(buf, str);
  va_start(ap, str);
  while ((cp = va_arg(ap, char *)) != NULL)
    strcat(buf, cp);
  va_end(ap);
  
  return buf;
}
