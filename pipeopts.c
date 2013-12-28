#include "bashansi.h"
#include <stdio.h>
#include <errno.h>

#include "builtins.h"
#include "shell.h"
#include "stdc.h"
#include "bashgetopt.h"
#include "common.h"

#if !defined (errno)
extern int errno;
#endif

#define MAX_OPTDEF    256
#define MAX_OPTION    2048
#define MAX_OPTGROUPS 16

/* message stolen from pathchk.c */
#define length_error(opt, len, max) builtin_error("name `%s' has length %d; exceeds limit of %d", opt, len, max - 1)

const char *optsep = "|";
char *shellvar_opts = "PIPEOPTS";
char *shellvar_args = "PIPEOPTSARG";

struct optgroup {
  /* getopts formatted option string for group */
  char optstr[MAX_OPTDEF];

  /* option string for group */
  char options[MAX_OPTION];
};

typedef struct pipeopts {
  /* original option string */
  char optdef[MAX_OPTDEF];

  /* getopts formatted option string */
  char optstr[MAX_OPTDEF];

  /* leftover arguments */
  char optarg[MAX_OPTION];

  /* options for each command in the pipeline */
  int optgroup_count;
  struct optgroup optgroup[MAX_OPTGROUPS];
} pipeopts;

static int init_pipeopts(pipeopts* opts, const char* optdef)
{
  char *part, *p;
  char tmp[MAX_OPTDEF];
  int len, count = 0;

  len = strlen(optdef);
  if(len > MAX_OPTDEF) {
    length_error("option definition", len, MAX_OPTDEF);
    return -1;
  }

  strncpy(opts->optdef, optdef, MAX_OPTDEF);
  strncpy(tmp, optdef, MAX_OPTDEF);

  p = tmp;
  len = 0;
  while((part = strsep(&p, optsep)) != NULL) {
    strncpy(opts->optgroup[count++].optstr, part, MAX_OPTDEF);
    len += snprintf(opts->optstr + len, MAX_OPTDEF - len, "%s", part);
  }

  opts->optgroup_count = count;

  return 0;
}

static int validate_pipeopts(pipeopts *opts)
{
  char *p, *opt;
  int i, j, count = opts->optgroup_count;

  if(count == 0) {
    builtin_error("no option string given");
    return -1;
  }

  for(i = 0; i < count; i++) {
    p = opts->optgroup[i].optstr;

    j = strlen(p);
    if(!j || index(p, ' ') != NULL) {
      if(!j)
        builtin_error("option string in group %d is empty", i);
      else
        builtin_error("option string contains a space: `%s'", p);

      return -1;
    }
  }

  for(i = 0; i < count - 1; i++) {
    for(j = i + 1; j < count; j++) {
      p = opts->optgroup[j].optstr;
      while((opt = strpbrk(p, opts->optgroup[i].optstr)) != NULL) {

        if(*opt != ':') {
          builtin_error("duplicate option `%c' in group %d", *opt, j);
          return -1;
        }

        p++;
      }
    }
  }

  return 0;
}

static int optstr_append(char *options, int opt, const char *optarg)
{
  int n, len = strnlen(options, MAX_OPTION);
  char *fmt;

  /* space + switch + NULL */
  if(len + 4 > MAX_OPTION) {
    length_error("an option", n, MAX_OPTION);
    return -1;
  }

  if(len > 0)
    options[len++] = ' ';

  options[len++] = '-';
  options[len++] = opt;
  options[len] = '\0';

  if(optarg) {
    /* maybe just always quote? */
    fmt = index(optarg, ' ') != NULL ? "'%s'" : "%s";
    n = snprintf(options + len, MAX_OPTION - len, fmt, optarg);

    if(n >= MAX_OPTION - len) {
      length_error("an option", n, MAX_OPTION);
      return -1;
    }
  }

  return 0;
}

static int process_options(pipeopts *opts, WORD_LIST *list)
{
  char *fmt;
  int pos, opt, n, len = 0;

  reset_internal_getopt();
  while((opt = internal_getopt(list, opts->optstr)) != -1) {
    /* unknown opt or missing optarg */
    if(opt == '?')
      return -1;

    /* TODO: if opt has "*" then it can accept anything */
    for(pos = 0; pos <= opts->optgroup_count; pos++) {
      if(index(opts->optgroup[pos].optstr, opt) != NULL) {
        optstr_append(opts->optgroup[pos].options, opt, list_optarg);
        break;
      }
    }
  }

  for(list = loptend; list; list = list->next) {
    n  = snprintf(opts->optarg + len,
                  MAX_OPTION - len,
                  list->next ? "%s " : "%s",
                  list->word->word);
    if(n >= MAX_OPTION - len) {
      /* TODO: quote */
      length_error("PIPEOPTSARG", n, MAX_OPTION);
      return -1;
    }

    len += n;
  }

  return 0;
}

static int export_options(const pipeopts* opts)
{
  int i;
  SHELL_VAR *pipeopts_v;
  ARRAY *pipeopts_a;

  unbind_variable(shellvar_opts);
  unbind_variable(shellvar_args);

  pipeopts_v = make_new_array_variable(shellvar_opts);
  if(!array_p(pipeopts_v)) {
    /* errorno used here? */
    builtin_error("cannot create shell variable %s", shellvar_opts);
    return -1;
  }

  pipeopts_a = array_cell(pipeopts_v);

  for(i = opts->optgroup_count -1; i >= 0; i--)
    array_push(pipeopts_a, opts->optgroup[i].options);

  (void*)bind_variable(shellvar_args, opts->optarg, 0);

  return 0;
}

pipeopts_builtin (list)
     WORD_LIST *list;
{
  int opt, i;
  char *arg;
  pipeopts opts;

  /* TODO: $@ */
  if(!list) {
    builtin_usage();
    return EX_USAGE;
  }

  memset(&opts, 0, sizeof(pipeopts));

  if(init_pipeopts(&opts, list->word->word) < 0)
    return EXECUTION_FAILURE;

  if(validate_pipeopts(&opts) < 0)
    return EXECUTION_FAILURE;

  /* skip over option definition */
  list = list->next;
  if(process_options(&opts, list) < 0)
    return EXECUTION_FAILURE;

#ifdef DEBUG
  printf("optdef: %s\n", opts.optdef);
  printf("optstr: %s\n", opts.optstr);
  for(i = 0; i < opts.optgroup_count; i++)
    printf("group %d: %s = %s\n", i, opts.optgroup[i].optstr, opts.optgroup[i].options);
  printf("optarg: %s\n", opts.optarg);
#endif

  if(export_options(&opts) < 0)
    return EXECUTION_FAILURE;

  return EXECUTION_SUCCESS;
}

char *pipeopts_doc[] = {
        "Getopts for process pipelines.",
        "",
        "Processes a pipe (`|') delimited getopts style",
        "argument string against each ARG, putting the",
        "parsed result into its corresponding position",
        "in the shell array variable PIPEOPTS.",
        "Remaining argument are concatinated and placed",
        "into PIPEOPTSARG.",
        (char *)NULL
};

struct builtin pipeopts_struct = {
        "pipeopts",
        pipeopts_builtin,
        BUILTIN_ENABLED,
        pipeopts_doc,
        "pipeopts optstring arg1 [, arg2, ... argN]",
        0
};
