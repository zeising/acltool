/*
 * acltool.c
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
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <ftw.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "argv.h"
#include "misc.h"
#include "commands.h"
#include "aclcmds.h"
#include "basic.h"

#include "acltool.h"

#ifdef USE_GETOPT_LONG
#include <getopt.h>

#define SHORT_OPTIONS "hnv::r::d::D::S:"

struct option long_options[] =
  {
   { "help",      no_argument,       NULL, 'h' },
   { "no-update", no_argument,       NULL, 'n' },
   { "style",     required_argument, NULL, 'S' },
   { "verbose",   optional_argument, NULL, 'v' },
   { "recurse",   optional_argument, NULL, 'r' },
   { "depth",     optional_argument, NULL, 'd' },
   { "debug",     optional_argument, NULL, 'D' },
   { NULL,        0,                 NULL, 0 },
};

#else

#include "opts.h"

int
set_debug(const char *name,
	  const char *value,
	  unsigned int type,
	  void *vp,
	  void *xp) {
  CONFIG *cp = (CONFIG *) xp;

  if (vp)
    cp->f_debug = * (int *) vp;
  else
    cp->f_debug++;
  
  return 0;
}

int
set_verbose(const char *name,
	    const char *value,
	    unsigned int type,
	    void *vp,
	    void *xp) {
  CONFIG *cp = (CONFIG *) xp;
  
  if (vp)
    cp->f_verbose = * (int *) vp;
  else
    cp->f_verbose++;
  
  return 0;
}

int
set_recurse(const char *name,
	    const char *value,
	    unsigned int type,
	    void *vp,
	    void *xp) {
  CONFIG *cp = (CONFIG *) xp;
  
  if (vp) {
    int v = * (int *) vp;
    if (v < -1)
      v = -1;
    
    cp->max_depth = v;
  }
  else
    cp->max_depth = -1;
  
  return 0;
}

int
set_depth(const char *name,
	    const char *value,
	    unsigned int type,
	    void *vp,
	    void *xp) {
  CONFIG *cp = (CONFIG *) xp;
  
  if (vp) {
    int v = * (int *) vp;
    
    cp->max_depth += v;
  }
  else
    cp->max_depth++;
  
  return 0;
}

int
set_style(const char *name,
	  const char *value,
	  unsigned int type,
	  void *vp,
	  void *xp) {
  CONFIG *cp = (CONFIG *) xp;

  
  if (value) {
    if (str2style(value, &cp->f_style) != 1)
      return -1;
  } else
    cp->f_style++;
  
  return 0;
}

int
set_no_update(const char *name,
	      const char *value,
	      unsigned int type,
	      void *vp,
	      void *xp) {
  CONFIG *cp = (CONFIG *) xp;

  cp->f_noupdate = 1;
  return 0;
}



OPTION options[] =
  {
   { "debug",     'D', OPTS_TYPE_UINT, set_debug,     "Debug level" },
   { "verbose",   'v', OPTS_TYPE_UINT, set_verbose,   "Verbosity level" },
   { "recurse",   'r', OPTS_TYPE_INT,  set_recurse,   "" },
   { "depth",     'd', OPTS_TYPE_INT,  set_depth,     "" },
   { "style",     'S', OPTS_TYPE_STR,  set_style,     "" },
   { "no-update", 'n', OPTS_TYPE_NONE, set_no_update, "" },
   { NULL,        -1,  0,              NULL,          NULL },
  };

#endif


char *version = "1.0";

COMMANDS commands;

char *argv0 = "acltool";

CONFIG d_cfg = { 0, 0, 0, 0, 0, 0 };





	  
#if 0
int
option_set_short(CONFIG *cfgp,
		 int f,
		 const char *val) {
  switch (f) {
  case 'h':
    break;
    
  case 'n':
    cfgp->f_noupdate = 1;
    break;

  case 'd':
    if (val) {
      int d;
      
      if (sscanf(val, "%d", &d) != 1) {
	fprintf(stderr, "%s: Error: %s: Invalid depth\n", argv0, val);
	return -1;
      }
      cfgp->max_depth += d;
      if (cfgp->max_depth < -1)
	cfgp->max_depth = -1;
    } else
      cfgp->max_depth++;
    break;

  case 'D':
    if (val) {
      if (sscanf(val, "%d", &cfgp->f_debug) != 1) {
	fprintf(stderr, "%s: Error: %s: Invalid debug level\n", argv0, val);
	return -1;
      }
    } else
      cfgp->f_debug++;
    break;
    
  case 'S':
    if (val) {
      if (sscanf(val, "%d", &cfgp->f_style) != 1 && str2style(val, &cfgp->f_style) != 1) {
	fprintf(stderr, "%s: Error: %s: Invalid ACL style\n", argv0, val);
	return -1;
      }
    } else
      cfgp->f_style++;
    break;
    
  case 'v':
    if (val) {
      if (sscanf(val, "%d", &cfgp->f_verbose) != 1) {
	fprintf(stderr, "%s: Error: %s: Invalid verbose level\n", argv0, val);
	return -1;
      }
    } else
      cfgp->f_verbose++;
    break;
    
  case 'r':
    if (val) {
      if (sscanf(val, "%d", &cfgp->max_depth) != 1 || cfgp->max_depth < -1) {
	fprintf(stderr, "%s: Error: %s: Invalid max depth\n", argv0, val);
	return -1;
      }
    } else
      cfgp->max_depth = -1;
    break;
    
  default:
    return -1;
  }

  return 0;
}

int
option_set_long(CONFIG *cfgp,
		const char *name,
		const char *val) {
  int i;
  struct option *op = NULL;
  
  for (i = 0; !op && long_options[i].name; i++) {
    if (strcmp(long_options[i].name, name) == 0) {
      if (op)
	return -1;
      op = &long_options[i];
    }
  }
  if (!op)
    return -1;

  return option_set_short(cfgp, op->val, val);
}
#endif

#if 0
char *
my_var_handler(const char *esc,
	       void *xtra)
{
    printf("Parsing var [%s]\n", esc ? esc : "<null>");

    if (strcmp(esc, "P") == 0 || strcmp(esc, "phone") == 0)
	return s_dup("+46705182786");
    
    if (strcmp(esc, "D") == 0 || strcmp(esc, "date") == 0)
	return s_dup("2006-11-24 13:37");

    return NULL;
}
#endif



int
cfg_parse(CONFIG *cfgp,
	  int *ai,
	  int argc,
	  char **argv) {
  int rc;
  
  rc = 0;

#if USE_GETOPT_LONG
  int c;
  
  optind = 1;
  optreset = 1;
  while (rc == 0 && (c = getopt_long(argc, argv, SHORT_OPTIONS, long_options, NULL)) != -1) {
    if (c == 'h' || c == '?') {
      cmd_help(&commands, argv[optind-2]);
      return -1;
    } else
      rc = option_set_short(cfgp, c, optarg);
  }
  
  *ai = optind;
  return rc;
#else
  rc = opts_parse_argv(&options[0], argc, argv, NULL);
  if (rc < 0)
    return rc;
  *ai = rc;
  return 0;
#endif
}


int
cmd_config(int argc,
	   char **argv,
	   void *vp) {
  int rc = 0;
  CONFIG *cfgp = (CONFIG *) vp;

  
  if (argc == 1) {
    puts("CONFIGURATION:");
    printf("  Debug Level:        %d\n", cfgp->f_debug);
    printf("  Verbosity Level:    %d\n", cfgp->f_verbose);
    if (cfgp->max_depth < 0)
      printf("  Recurse Max Depth:  No Limit\n");
    else
      printf("  Recurse Max Depth:  %d\n", cfgp->max_depth);
    printf("  Update:             %s\n", cfgp->f_noupdate ? "No" : "Yes");
    printf("  Style:              %s\n", style2str(cfgp->f_style));
  } else {
    int i;

    
    for (i = 1; i < argc; i++) {
      char *cp = strchr(argv[i], '=');
      if (cp)
	*cp++ = '\0';
#ifdef USE_GETOPT_LONG
      rc = option_set_long(cfgp, argv[i], cp);
#else
      rc = opts_set(options, argv[i], vp);
#endif
      if (rc)
	return rc;
    }
  }

  d_cfg = *cfgp;
  return rc;
}



#if 0
int
cmd_print(int argc,
	  char **argv,
	  void *vp) {
  int i;

  
  for (i = 1; i < argc; i++) {
    if (i > 1)
      putchar(' ');
    fputs(argv[i], stdout);
  }
  putchar('\n');
  return 0;
}


int
cmd_pwd(int argc,
	char **argv,
	void *vp) {
  char buf[2048];

  if (getcwd(buf, sizeof(buf)))
    puts(buf);

  return 0;
}

int
cmd_cd(int argc,
       char **argv,
       void *vp) {
  int i, rc;
  

  if (argc == 1) {
    char *homedir = getenv("HOME");

    if (!homedir) {
      fprintf(stderr, "%s: Error: Unable to read HOME environment\n",
	      argv0);
      return 1;
    }
    
    rc = chdir(homedir);
    if (rc < 0) {
      fprintf(stderr, "%s: Error: %s: Changing directory: %s\n",
	      argv[0], homedir, strerror(errno));
      return 1;
    }

    return 0;
  }

  for (i = 1; i < argc; i++) {
    rc = chdir(argv[i]);
    if (rc < 0) {
      fprintf(stderr, "%s: Error: %s: Changing directory: %s\n",
	      argv[0], argv[i], strerror(errno));
      return 1;
    }
  }
  
  return 0;
}


int
cmd_exit(int argc,
	 char **argv,
	 void *vp) {
  int ec = 0;

  
  if (argc > 1) {
    if (sscanf(argv[1], "%d", &ec) != 1) {
      fprintf(stderr, "%s: Error: %s: Invalid exit code\n", argv[0], argv[1]);
      return -1;
    }
  }

  exit(ec);
}
#endif


COMMAND acltool_commands[] = {
  { "config", 	"[<opt>[=<val>]]*",		cmd_config,	"Print/update default configuration" },
  { NULL,	NULL,				NULL,		NULL },
};


  
char *
cmd_name_generator(const char *text,
		   int state) {
  static int ci;
  COMMAND *cp;
  
  
  if (!state)
    ci = 0;

  while (ci < commands.cc) {
    cp = commands.cv[ci];
    ++ci;
    
    if (s_match(text, cp->name))
      return s_dup(cp->name);
  }
  
  return NULL;
}

char **
cmd_name_completion(const char *text,
		    int start,
		    int end) {
  rl_attempted_completion_over = 1;
  
  return rl_completion_matches(text, start == 0 ? cmd_name_generator : rl_filename_completion_function);
}



int
run_cmd(int argc,
	char **argv,
	CONFIG *cp) {
  CONFIG cfg;
  int ai, rc;
  
  
  cfg = *cp;
  
  ai = 0;
  if (cfg_parse(&cfg, &ai, argc, argv) != 0)
    return -1;
  
  rc = cmd_run(&commands, argc-ai+1, argv+ai-1, (void *) &cfg);
  
  if (ai > 1) {
    free(argv[ai-1]);
    argv[ai-1] = argv[0];
  }

  return rc;
}


int
main(int argc,
     char **argv)
{
  char *buf;
  char **av;
  int ai, ac, rc;
  

  argv0 = argv[0];

  cmd_init(&commands);
  cmd_register(&commands, 0, basic_commands);
  cmd_register(&commands, 0, acltool_commands);
  cmd_register(&commands, 0, acl_commands);
  
  ai = 0;
  if (cfg_parse(&d_cfg, &ai, argc, argv) != 0)
    exit(1);

  if (ai == argc) {
    if (isatty(fileno(stdin)))
      puts("INTERACTIVE MODE");
    
    rl_attempted_completion_function = cmd_name_completion;
    
    while ((buf = readline("> ")) != NULL) {
      add_history(buf);

      while (*buf && isspace(*buf))
	++buf;
      
      switch (*buf) {
      case '!':
	rc = system(buf+1);
	break;

      case '#':
	break;

      default:
	ac = argv_create(buf, NULL, NULL, &av);
	if (ac > 0)
	  rc = run_cmd(ac, av, &d_cfg);
	
	argv_destroy(av);
      }
      
      free(buf);
      if (rc)
	fprintf(stderr, "ERR");
    }

    exit(rc);
  }
  
  rc = run_cmd(argc-ai, argv+ai, &d_cfg);
  return rc;
}
