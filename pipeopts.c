#include <config.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif
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

extern char *this_command_name;
extern char *strerror();

#define MAX_OPTDEF    256
#define MAX_OPTION    2048
#define MAX_OPTGROUPS 16

const char *optsep = "|";
char *shellvar = "PIPEOPTS";

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

  // TODO: array here?
  /* leftover arguments */
  char optarg[MAX_OPTION];

  /* options for each command in the pipeline */
  int optgroup_count;
  struct optgroup optgroup[MAX_OPTGROUPS];
} pipeopts;

/* TODO: error checking */
static int init_pipeopts(pipeopts* opts, const char* optdef)
{
  char *part, *p;
  char tmp[MAX_OPTDEF];
  int len, count = 0;

  strncpy(opts->optdef, optdef, MAX_OPTDEF);
  strncpy(tmp, optdef, MAX_OPTDEF);

  p = tmp;
  while((part = strsep(&p, optsep)) != NULL) {
    strncpy(opts->optgroup[count++].optstr, part, MAX_OPTDEF - 1);

    len = strnlen(opts->optstr, MAX_OPTDEF);
    strncat(opts->optstr, part, MAX_OPTDEF - len - 1);
  }

  opts->optgroup_count = count;

  return 0;
}

static int validate_pipeopts(pipeopts *opts)
{
  char *p;
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
        builtin_error("option string in group %s is empty", i);
      else
        builtin_error("option string contains a space: `%s'", p);

      return -1;
    }
  }

  for(i = 0; i < count - 1; i++) {
    for(j = i + 1; j < count; j++) {
      char *x;
      p = opts->optgroup[j].optstr;
      while((x = strpbrk(p, opts->optgroup[i].optstr)) != NULL) {

        if(*x != ':') {
          builtin_error("duplicate option `%c' in group %d", *x, j);
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
  int pos = 0, len = strnlen(options, MAX_OPTION);

  /* TODO: max len */
  if(len > 0)
    options[len++] = ' ';

  options[len++] = '-';
  options[len++] = opt;

  /* TODO: optarg should be quoted */
  if(optarg)
    strncat(options, optarg, MAX_OPTION - len - 1);

  return 0;
}

static int process_options(pipeopts *opts, WORD_LIST *list)
{
  int pos, opt;

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

  // loptend // last non-option in the list

  return 0;
}

static int export_options(const pipeopts* opts)
{
  int i;
  SHELL_VAR *pipeopts_v;
  ARRAY *pipeopts_a;

  unbind_variable(shellvar);
  pipeopts_v = make_new_array_variable(shellvar);
  if(!array_p(pipeopts_v)) {
    /* errorno used here? */
    builtin_error("cannot create shell variable %s", shellvar);
    return -1;
  }

  pipeopts_a = array_cell(pipeopts_v);

  for(i = opts->optgroup_count -1; i >= 0; i--)
    array_push(pipeopts_a, opts->optgroup[i].options);

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
