#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <error.h>

#define KiB 1024
#define MiB 1024 * 1024
#define DAY 24 * 60 * 60

typedef enum
{
  NONE,
  EPOCH,
  HUMAN_READABLE,
  HUMAN_READABLE_T
} time_fmt_t;

struct config
{
  char *arg;
  time_fmt_t time_fmt;
  size_t buf_size;
  size_t log_size;
  unsigned long log_age;
  unsigned int log_count;
  char *base_dir;
  bool no_subdirs;
};

char *
default_base_dir ()
{
  if (getuid () == 0)
    return "/var/log";

  char *base_dir = getenv ("XDG_DATA_HOME");
  if (base_dir)
    return strcat (base_dir, "/glycerin/");

  base_dir = getenv ("HOME");
  if (base_dir)
    return strcat (base_dir, "/.local/share/glycerin/");

  return NULL;
}

struct config
default_config ()
{
  struct config conf = {
    .arg = NULL,
    .time_fmt = NONE,
    .buf_size = KiB,
    .log_size = 10 * MiB,
    .log_age = DAY,
    .log_count = 7,
    .base_dir = default_base_dir (),
    .no_subdirs = false,
  };

  if (! conf.base_dir)
    error (EXIT_FAILURE, 0, "fatal: could not determine directory for storing logs");

  return conf;
}

void
print_help_and_exit (const char *arg0, int status)
{
  printf ("Usage: %s [OPTIONS] APPNAME\n", arg0);
  exit (status);
}

unsigned long
parse_ulong (const char *s, const char optopt)
{
  char *end = NULL;
  unsigned long l = strtoul (s, &end, 10);

  if (errno != 0)
    {
      errno = 0;
      error (EXIT_FAILURE, 0, "invalid value for option: %c", optopt);
    }

  return l;
}

void
parse_cli (struct config *conf, int argc, char *const *argv)
{
  int opt;
  unsigned long l;

  while ((opt = getopt (argc, argv, ":tb:s:a:n:d:fh")) != -1)
    {
      switch (opt)
        {
        case 't':
          conf->time_fmt++;
          break;
        case 'b':
          conf->buf_size = parse_ulong (optarg, optopt);
          if (! conf->buf_size)
            error (EXIT_FAILURE, 0, "fatal: buffer size must be greater than zero");
          break;
        case 's':
          conf->log_size = parse_ulong (optarg, optopt);
          if (! conf->log_size)
            error (EXIT_FAILURE, 0, "fatal: log file before rotating must be greater than zero");
          break;
        case 'a':
          conf->log_age = parse_ulong (optarg, optopt);
          break;
        case 'n':
          l = parse_ulong (optarg, optopt);
          if (l > INT_MAX)
            error (EXIT_FAILURE, 0, "fatal: you really want to keep that many logs?");
          conf->log_count = (int) l;
          break;
        case 'd':
          conf->base_dir = optarg;
          break;
        case 'f':
          conf->no_subdirs = true;
          break;
        case 'h':
          print_help_and_exit (argv[0], EXIT_FAILURE);
          break;
        case ':':
          error (EXIT_FAILURE, 0, "options is missing a value: %c", optopt);
        case '?':
          error (EXIT_FAILURE, 0, "unknown option: %c\nsee %s -h for help", optopt, argv[0]);
        }
    }

  if (optind != argc - 1)
    error (EXIT_FAILURE, 0, "no APPNAME given");
  else
    conf->arg = argv[argc - 1];

  if (conf->time_fmt > HUMAN_READABLE_T)
    error (EXIT_FAILURE, 0, "chosen time format is invalid");
}

int
main (int argc, char **argv)
{
  struct config conf = default_config ();
  parse_cli (&conf, argc, argv);

  printf ("APPNAME: %s\n", conf.arg);

  return EXIT_SUCCESS;
}
