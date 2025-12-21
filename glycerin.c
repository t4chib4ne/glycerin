#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>

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

// Speichert das Erstellungsdatum des Logs.
struct timespec current_log_birth;

// Pfad zum vorherigen Log.
char *prev_log_name = NULL;
size_t prev_log_name_len = 0;

// Buffer in dem sich die aktuelle Uhrzeit im
// gewaehlten Format befindet nach Aufruf von
// `time_func`
#define TIME_FMT_BUF_SIZE 32
char time_fmt_buf[TIME_FMT_BUF_SIZE] = { 0 };
size_t time_fmt_buf_len = 0;

// Dateinamen der alten Logs.
char **old_logs = NULL;

// Anzahl Logs exclusive des aktuellen Logs.
unsigned int log_count = 0;

// Signal vars.
static volatile sig_atomic_t exit_sig = 0;
static volatile sig_atomic_t rotate_sig = 0;

static void
exit_sig_hanlder (int signo)
{
  exit_sig = 1;
}

static void
rotate_sig_hanlder (int signo)
{
  rotate_sig = 1;
}

// Ermittelt die aktuelle Uhrzeit.
char *(*time_func) (const struct timespec *ts);

char *
time_func_none (const struct timespec *ts)
{
  return time_fmt_buf;
}

char *
time_func_epoch (const struct timespec *ts)
{
  size_t n = snprintf (time_fmt_buf, TIME_FMT_BUF_SIZE, "%lu%03ld", ts->tv_sec, ts->tv_nsec / 1000000L);
  if (n >= TIME_FMT_BUF_SIZE)
    {
      fprintf (stderr, "time_func_epoch: cannot fit time into format\n");
      return NULL;
    }

  time_fmt_buf_len = n;
  return time_fmt_buf;
}

char *
time_func_fmt (const struct timespec *ts, const char *fmt)
{
  struct tm tp;
  if (gmtime_r (&ts->tv_sec, &tp) == NULL)
    {
      perror ("gmtime_r");
      return NULL;
    }

  size_t n = strftime (time_fmt_buf, TIME_FMT_BUF_SIZE, fmt, &tp);
  if (n == 0)
    {
      fprintf (stderr, "time_func_fmt: format for strftime too long\n");
      return NULL;
    }

  size_t m = snprintf (time_fmt_buf + n, TIME_FMT_BUF_SIZE - n, ".%05ld", ts->tv_nsec / 1000000L);
  if (m >= TIME_FMT_BUF_SIZE - n)
    {
      fprintf (stderr, "time_func_fmt: cannot fit milliseconds into format\n");
      return NULL;
    }

  time_fmt_buf_len = n + m;
  return time_fmt_buf;
}

char *
time_func_hmr (const struct timespec *ts)
{
  return time_func_fmt (ts, "%Y-%m-%d_%H:%M:%S");
}

char *
time_func_hmrt (const struct timespec *ts)
{
  return time_func_fmt (ts, "%Y-%m-%dT%H:%M:%S");
}

bool
has_suffix (const char *s, const char *suffix)
{
  const size_t s_len = strlen (s);
  const size_t suffix_len = strlen (suffix);
  if (s_len < suffix_len)
    return false;

  return ! strcmp (&s[s_len - suffix_len], suffix);
}

int
log_name_cmp (const void *lhs, const void *rhs)
{
  const char *left = *((char **) lhs);
  const char *right = *((char **) rhs);

  return strcmp (right, left);
}

char *
base_dir ()
{
  // We are root.
  if (getuid () == 0)
    {
      const size_t s_len = strlen ("/var/log/") + strlen (conf.arg);
      char *s = malloc ((s_len + 1) * sizeof (char));
      if (s == NULL)
        error (EXIT_FAILURE, 0, "%s", strerror (errno));
      strcat (s, "/var/log/");
      strcat (s, conf.arg);
      return s;
    }

  // We are not root and determine our
  // base dir via the env.
  char *base_dir = getenv ("XDG_DATA_HOME");
  const char *subdir = "glycerin/";
  if (! base_dir)
    {
      base_dir = getenv ("HOME");
      subdir = ".local/share/glycerin/logs/";
    }

  const size_t app_dir_len = conf.no_subdirs ? 0 : strlen (conf.arg) + 1;
  const size_t base_dir_len = strlen (base_dir);
  const int needs_slash = base_dir[base_dir_len - 1] == '/' ? 0 : 1;
  const size_t s_len = strlen (base_dir) + needs_slash + strlen (subdir) + app_dir_len;

  char *s = malloc ((s_len + 1) * sizeof (char));
  if (s == NULL)
    error (EXIT_FAILURE, 0, "%s", strerror (errno));

  strcpy (s, base_dir);
  if (needs_slash)
    strcat (s, "/");
  strcat (s, subdir);

  if (app_dir_len)
    {
      strcat (s, conf.arg);
      strcat (s, "/");
    }

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
  errno = 0;
  char *end = NULL;
  unsigned long l = strtoul (s, &end, 10);

  if (errno != 0)
    error (EXIT_FAILURE, 0, "invalid value for option %c: %s", optopt, strerror (errno));

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
            error (EXIT_FAILURE, 0, "buffer size must be greater than zero");
          break;
        case 's':
          conf.log_size = parse_ulong (optarg, optopt);
          if (! conf.log_size)
            error (EXIT_FAILURE, 0, "log file before rotating must be greater than zero");
          break;
        case 'a':
          conf.log_age = parse_ulong (optarg, optopt);
          break;
        case 'n':
          l = parse_ulong (optarg, optopt);
          if (l > INT_MAX)
            error (EXIT_FAILURE, 0, "You really want to keep that many logs?");
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
          error (EXIT_FAILURE, 0, "option is missing a value: %c", optopt);
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
  char *offset = pathname;
  char *slash_ptr;

  if (! stat (pathname, &st))
    return 0;

  while (true)
    {
      slash_ptr = strchr (offset, '/');
      if (! slash_ptr)
        break;

      *slash_ptr = '\0';

      if (! stat (pathname, &st))
        goto last;

      if (*pathname != '\0' && mkdir (pathname, 0755) == -1)
        {
          perror (pathname);
          *slash_ptr = '/';
          return -1;
        }

    last:
      *slash_ptr = '/';
      offset = slash_ptr + 1;
    }

  return 0;
}

char *
set_prev_log_name ()
{
  if (conf.no_subdirs)
    {
      strcpy (prev_log_name, conf.arg);
      strcat (prev_log_name, ".");
      strcat (prev_log_name, time_func_epoch (&current_log_birth));
    }
  else
    strcpy (prev_log_name, time_func_epoch (&current_log_birth));

  strcat (prev_log_name, LOG_EXT);

  return prev_log_name;
}

int
open_current_log ()
{
  current_log = fopen (current_log_name, "w");
  if (! current_log)
    {
      perror (current_log_name);
      return -1;
    }

  if (clock_gettime (CLOCK_REALTIME, &current_log_birth))
    {
      perror ("clock_gettime");
      return -1;
    }

  return 0;
}

void
evict_setup ()
{
  DIR *d;
  struct dirent *de;
  d = opendir (".");

  size_t size = conf.log_count;
  int idx = 0;

  old_logs = calloc (conf.log_count, sizeof (*old_logs));
  if (! old_logs)
    error (EXIT_FAILURE, 0, "%s", strerror (errno));

  // Read all log files.
  while ((de = readdir (d)) != NULL)
    {
      if (de->d_type != DT_REG || ! has_suffix (de->d_name, LOG_EXT) || ! strcmp (de->d_name, current_log_name))
        continue;

      if (idx == size)
        {
          old_logs = realloc (old_logs, size * 2 * sizeof (*old_logs));
          size *= 2;
        }

      old_logs[idx] = malloc ((strlen (de->d_name) + 1) * sizeof (char));
      strcpy (old_logs[idx], de->d_name);
      idx++;
    }

  closedir (d);

  // Sort all log files.
  qsort (old_logs, idx, sizeof (*old_logs), log_name_cmp);

  if (idx <= conf.log_count)
    {
      log_count = idx;
      return;
    }

  // Delete logs that are too many.
  for (int i = conf.log_count; i < idx; i++)
    {
      if (unlink (old_logs[i]))
        error (EXIT_FAILURE, 0, "cannot delete old log: %s: %s", old_logs[i], strerror (errno));
      free (old_logs[i]);
    }

  // Shrink down log tracker.
  old_logs = realloc (old_logs, conf.log_count * sizeof (*old_logs));
}

void
evict_file (const char *newfile)
{
  if (log_count <= conf.log_count)
    return;

  // We continue if unlink fails.
  // During current execution this file will
  // be ignored.
  if (unlink (old_logs[conf.log_count - 1]))
    fprintf (stderr, "cannot unlink: %s: %s\n", old_logs[conf.log_count - 1], strerror (errno));

  for (int i = conf.log_count - 1; i > 0; --i)
    strcpy (old_logs[i], old_logs[i - 1]);

  strcpy (old_logs[0], newfile);
  log_count--;
}

int
rotate ()
{
  if (fclose (current_log))
    {
      perror ("fclose");
      return -1;
    }

  if (rename (current_log_name, set_prev_log_name ()))
    {
      perror ("rename");
      return -1;
    }

  log_count++;
  evict_file (prev_log_name);

  return 0;
}

void
free_globals ()
{
  free (BUF);
  free (conf.base_dir);
  free (prev_log_name);

  if (conf.no_subdirs)
    free (current_log_name);

  for (int i = 0; i < log_count; i++)
    free (old_logs[i]);
  free (old_logs);
}

void
setup_globals ()
{
  conf.base_dir = base_dir ();
  if (! conf.base_dir)
    error (EXIT_FAILURE, 0, "could not determine directory for storing logs");

  // Allocate our buffer.
  BUF = malloc (conf.buf_size * sizeof (char));
  if (! BUF)
    error (EXIT_FAILURE, 0, "allocating main buffer: %s", strerror (errno));

  // Switch cwd to the logging base dir,
  // create it if it does not exist.
  if (mkdirp (conf.base_dir) == -1)
    exit (EXIT_FAILURE);
  if (chdir (conf.base_dir) == -1)
    error (EXIT_FAILURE, 0, "cannot chdir to %s: %s", conf.base_dir, strerror (errno));

  // The current log file name depends on,
  // whether we use subdirs or not.
  if (conf.no_subdirs)
    {
      const size_t current_log_name_len = strlen (conf.arg) + strlen (LOG_EXT);
      prev_log_name_len = current_log_name_len + TIME_FMT_BUF_SIZE + 1;

      current_log_name = malloc ((current_log_name_len + 1) * sizeof (char));
      prev_log_name = malloc ((prev_log_name_len + 1) * sizeof (char));
      if (! current_log_name || ! prev_log_name)
        error (EXIT_FAILURE, 0, "%s", strerror (errno));

      strcpy (current_log_name, conf.arg);
      strcat (current_log_name, LOG_EXT);
    }
  else
    {
      current_log_name = "current.log";
      prev_log_name_len = TIME_FMT_BUF_SIZE + strlen (LOG_EXT);
      prev_log_name = malloc ((prev_log_name_len + 1) * sizeof (char));
      if (! prev_log_name)
        error (EXIT_FAILURE, 0, "%s", strerror (errno));
    }

  // Open the log or create a new one.
  struct stat st;
  if (! stat (current_log_name, &st))
    error (EXIT_FAILURE, 0, "log directory already in use");
  else if (open_current_log ())
    exit (EXIT_FAILURE);

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

void
setup_signaling ()
{
  // Register handler for exiting.
  struct sigaction exit_sa = { 0 };
  exit_sa.sa_handler = exit_sig_hanlder;
  sigemptyset (&exit_sa.sa_mask);
  exit_sa.sa_flags = 0;

  if (sigaction (SIGTERM, &exit_sa, NULL) == -1 || sigaction (SIGINT, &exit_sa, NULL) == -1)
    error (EXIT_FAILURE, 0, "term handlers: %s", strerror (errno));

  // Logs shall never be rotated based on
  // time.
  if (conf.log_age == 0)
    return;

  // Register handler for time based rotation.
  struct sigaction rotate_sa = { 0 };
  rotate_sa.sa_handler = rotate_sig_hanlder;
  sigemptyset (&rotate_sa.sa_mask);
  rotate_sa.sa_flags = 0;

  if (sigaction (SIGALRM, &rotate_sa, NULL))
    error (EXIT_FAILURE, 0, "rotation handler: %s", strerror (errno));

  // Register the signal timer.
  struct timeval tv = { .tv_sec = conf.log_age, .tv_usec = 0 };
  struct itimerval itv = { .it_value = tv, .it_interval = tv };
  if (setitimer (ITIMER_REAL, &itv, NULL) == -1)
    error (EXIT_FAILURE, 0, "cannot setup timer: %s", strerror (errno));
}

int
main (const int argc, char *const *argv)
{
  parse_cli (argc, argv);

  setup_globals ();

  evict_setup ();

  setup_signaling ();

  bool is_new_line = true;
  unsigned int n = 0;
  struct timespec ts;

  while (true)
    {
      if (fgets (BUF, conf.buf_size, stdin) == NULL)
        {
          if (exit_sig)
            goto exit;
          if (rotate_sig)
            goto rotate;
          fprintf (stderr, "fatal: reading stdin\n");
          break;
        }

      if (is_new_line)
        {
          if (clock_gettime (CLOCK_REALTIME, &ts))
            fprintf (stderr, "error: could not get time\n");
          else
            time_func (&ts);

          fwrite (time_fmt_buf, sizeof (char), strlen (time_fmt_buf), current_log);
          fputc (' ', current_log);
        }

      n = strlen (BUF);
      is_new_line = BUF[n - 1] == '\n';

      fwrite (BUF, sizeof (char), n, current_log);

      // Exit if we are done with the current line.
    exit:
      if (is_new_line && exit_sig)
        break;

      // Rotate if we are done with the current line.
    rotate:
      if (is_new_line && rotate_sig)
        {
          rotate_sig = 0;
          if (rotate ())
            {
              fprintf (stderr, "fatal: could not rotate\n");
              goto free;
            }
          if (open_current_log ())
            {
              fprintf (stderr, "fatal: could not open new log\n");
              goto free;
            }
        }
    }

  rotate ();
free:
  free_globals ();
  return EXIT_SUCCESS;
}
