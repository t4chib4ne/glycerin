#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <error.h>

// Byte Groessen
#define KiB 1024
#define MiB 1024 * 1024

// Zeit Groessen
#define DAY 24 * 60 * 60

// Datei Konstanten
#define LOG_EXT ".log"

// Die verfuegbaren Formate der Zeitangabe einer
// Logzeiele.
typedef enum
{
  NONE,
  EPOCH,
  HUMAN_READABLE,
  HUMAN_READABLE_T
} time_fmt_t;

// In diesem Struct werden alle Konfigurationen
// und Eigenschaften des Programms gespeichert.
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

// Globale Instanz der Config.
struct config conf = {
  .arg = NULL,
  .time_fmt = NONE,
  .buf_size = KiB,
  .log_size = 10 * MiB,
  .log_age = DAY,
  .log_count = 7,
  .base_dir = NULL,
  .no_subdirs = false
};

// Buffer fuer das Einlesen von stdin.
char *BUF = NULL;

// Datei des aktuellen Log in die geschrieben
// wird.
char *current_log_name = NULL;
FILE *current_log = NULL;

// Speichert die Laenge des Logs.
size_t current_log_size = 0;

// Buffer in dem sich die aktuelle Uhrzeit im
// gewaehlten Format befindet nach Aufruf von
// `time_func`
char time_fmt_buf[32] = { 0 };

// Ermittelt die aktuelle Uhrzeit.
void (*time_func) ();

void
time_func_none ()
{
}

void
time_func_epoch ()
{
  struct timespec ts;
  if (clock_gettime (CLOCK_REALTIME, &ts))
    error (EXIT_FAILURE, 0, "clock_gettime not working");

  snprintf (time_fmt_buf, sizeof (time_fmt_buf), "%lu%03ld", ts.tv_sec, ts.tv_nsec / 1000000L);
}

void
time_func_fmt (const char *fmt)
{
  struct timespec ts;
  if (clock_gettime (CLOCK_REALTIME, &ts))
    error (EXIT_FAILURE, 0, "clock_gettime not working");

  struct tm tp;
  if (! gmtime_r (&ts.tv_sec, &tp))
    error (EXIT_FAILURE, 0, "gmtime_r not working");

  size_t n = strftime (time_fmt_buf, sizeof (time_fmt_buf), fmt, &tp);
  if (! n)
    error (EXIT_FAILURE, 0, "strftime not working");

  snprintf (time_fmt_buf + n, sizeof (time_fmt_buf) - n, ".%05ld", ts.tv_nsec / 1000000L);
}

void
time_func_hmr ()
{
  time_func_fmt ("%Y-%m-%d_%H:%M:%S");
}

void
time_func_hmrt ()
{
  time_func_fmt ("%Y-%m-%dT%H:%M:%S");
}

char *
default_base_dir ()
{
  if (getuid () == 0)
    return "/var/log/";

  char *base_dir = getenv ("XDG_DATA_HOME");
  const char *subdir = "glycerin/";
  if (! base_dir)
    {
      base_dir = getenv ("HOME");
      subdir = ".local/share/glycerin/logs/";
    }

  size_t base_dir_len = strlen (base_dir);
  int needs_slash = base_dir[base_dir_len - 1] == '/' ? 1 : 0;

  char *s = malloc (strlen (base_dir) + needs_slash + strlen (subdir));
  if (! s)
    error (EXIT_FAILURE, 0, "cannot malloc");

  strcpy (s, base_dir);
  if (needs_slash)
    strcat (s, "/");
  strcat (s, subdir);

  return s;
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
parse_cli (int argc, char *const *argv)
{
  int opt;
  unsigned long l;

  while ((opt = getopt (argc, argv, ":tb:s:a:n:d:fh")) != -1)
    {
      switch (opt)
        {
        case 't':
          conf.time_fmt++;
          break;
        case 'b':
          conf.buf_size = parse_ulong (optarg, optopt);
          if (! conf.buf_size)
            error (EXIT_FAILURE, 0, "fatal: buffer size must be greater than zero");
          break;
        case 's':
          conf.log_size = parse_ulong (optarg, optopt);
          if (! conf.log_size)
            error (EXIT_FAILURE, 0, "fatal: log file before rotating must be greater than zero");
          break;
        case 'a':
          conf.log_age = parse_ulong (optarg, optopt);
          break;
        case 'n':
          l = parse_ulong (optarg, optopt);
          if (l > INT_MAX)
            error (EXIT_FAILURE, 0, "fatal: you really want to keep that many logs?");
          conf.log_count = (int) l;
          break;
        case 'd':
          conf.base_dir = optarg;
          break;
        case 'f':
          conf.no_subdirs = true;
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
    conf.arg = argv[argc - 1];

  for (int i = 0; i < strlen (conf.arg); ++i)
    if (conf.arg[i] == '/' || conf.arg[i] == '.')
      error (EXIT_FAILURE, 0, "APPNAME contains unallowed characters");

  if (conf.time_fmt > HUMAN_READABLE_T)
    error (EXIT_FAILURE, 0, "chosen time format is invalid");
}

// Changes pathname during execution but promises
// it will not have changed after exiting.
int
mkdirp (char *pathname)
{
  struct stat st;
  int offset = 0;
  char *slash_ptr;

  if (! stat (pathname, &st))
    return 0;

  while (true)
    {
      slash_ptr = strchr (pathname + offset, '/');
      if (! slash_ptr)
        continue;

      *slash_ptr = '\0';

      if (! stat (pathname, &st))
        {
          *slash_ptr = '/';
          continue;
        }

      if (mkdir (pathname, 0755) == -1)
        {
          *slash_ptr = '/';
          return -1;
        }
    }

  return mkdir (pathname, 0755);
}

void
free_globals ()
{
  free (BUF);
  free (conf.base_dir);
}

void
setup_globals ()
{
  // Allocate our buffer.
  BUF = malloc (conf.buf_size * sizeof (char));
  if (! BUF)
    error (EXIT_FAILURE, 0, "fatal: could not allocate buffer");

  // Append the app subdir to the base dir.
  if (! conf.no_subdirs)
    {
      const size_t base_dir_len = strlen (conf.base_dir);
      char *s = realloc (conf.base_dir, base_dir_len + strlen (conf.arg) + 1);
      if (! s)
        error (EXIT_FAILURE, 0, "fatal: could not allocate log path");
      strcat (s + base_dir_len, "/");
      strcat (s + base_dir_len + 1, conf.arg);
      conf.base_dir = s;
    }

  // Switch cwd to the logging base dir,
  // create it if it does not exist.
  if (mkdirp (conf.base_dir) == -1)
    error (EXIT_FAILURE, 0, "fatal: could not create logdir");
  if (chdir (conf.base_dir) == -1)
    error (EXIT_FAILURE, 0, "fatal: cannot chdir to %s", conf.base_dir);

  // Select time function
  switch (conf.time_fmt)
    {
    case NONE:
      time_func = time_func_none;
    case EPOCH:
      time_func = time_func_epoch;
    case HUMAN_READABLE:
      time_func = time_func_hmr;
    case HUMAN_READABLE_T:
      time_func = time_func_hmrt;
    }
}

int
main (const int argc, char *const *argv)
{
  conf.base_dir = default_base_dir ();
  if (! conf.base_dir)
    error (EXIT_FAILURE, 0, "fatal: could not determine directory for storing logs");

  parse_cli (argc, argv);

  setup_globals ();

  free_globals ();
  return EXIT_SUCCESS;
}
