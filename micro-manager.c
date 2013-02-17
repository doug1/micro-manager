
/*
 * micro-manager
 * Amazon EC2 t1.micro CPU manager
 * https://github.com/doug1/micro-manager
 *
 * Copyright (C) 2013 Doug Mitchell
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/**********************************************************************/

#define SCHED_QUANTUM_NS (100 * 1000 * 1000)
#define JIFFIES_PER_SEC (sysconf(_SC_CLK_TCK))
#define MAX_PRIORITY (-10)
#define PIDLIST_SIZE (1024)

#define CPU_T1MICRO (0.30)
#define CPU_MIN (0.01)
#define CPU_MAX (1.00)

/**********************************************************************/

static pid_t pidlist_pids[PIDLIST_SIZE];
static int pidlist_count = 0;
static int running = 1;

/**********************************************************************/

static void
signal_handler (int sig)
{
  running = 0;
}

void
mm_fatal (const char *message)
{
  fprintf (stderr, "micro-manager: %s\n", message);
  exit (EXIT_FAILURE);
}

void
mm_perror (const char *message)
{
  char buffer[100];
  snprintf ("micro-manager: %s", sizeof (buffer), message);
  perror (buffer);
  exit (EXIT_FAILURE);
}

static inline unsigned long
subtract_timevals (const struct timeval *t1, const struct timeval *t2)
{
  return (t1->tv_usec - t2->tv_usec) + (t1->tv_sec - t2->tv_sec) * 1000000;
}

double
get_cpu_usage ()
{
  char *word;
  unsigned long idle_jiffies;
  static unsigned long last_idle = 0;
  static char buffer[PATH_MAX];
  static struct timeval tv, last_tv;
  double idle_diff, time_diff;

  FILE *fd = fopen ("/proc/stat", "r");
  if (fd == NULL)
    mm_fatal ("/proc not mounted");
  if (fgets (buffer, sizeof (buffer), fd) == NULL)
    mm_perror ("read /proc/stat");
  fclose (fd);

  word = strtok (buffer, " ");
  // skip word "cpu"
  word = strtok (NULL, " ");
  // user_jiffies = atol (word);
  word = strtok (NULL, " ");
  // nice_jiffies = atol (word);
  word = strtok (NULL, " ");
  // sys_jiffies = atol (word);
  word = strtok (NULL, " ");
  idle_jiffies = atol (word);

  if (last_idle == 0)
    {
      last_idle = idle_jiffies;
      gettimeofday (&last_tv, NULL);
      return 0;
    }

  gettimeofday (&tv, NULL);

  idle_diff = ((double) (idle_jiffies - last_idle)) / JIFFIES_PER_SEC;
  time_diff = ((double) subtract_timevals (&tv, &last_tv)) / 1000000;

  last_idle = idle_jiffies;
  last_tv = tv;

  if (time_diff > 0)
    return (double) 1 - (idle_diff / time_diff);
  else
    return 0;
}

void
update_pidlist ()
{
  DIR *dir;
  struct dirent *entry;

  if ((dir = opendir ("/proc")) == NULL)
    mm_perror ("opendir");

  pidlist_count = 0;
  while ((entry = readdir (dir)) != NULL)
    {
      pid_t pid;
      char procdir[PATH_MAX];
      struct stat statbuf;

      if (strtok (entry->d_name, "0123456789"))
	continue;
      pid = atoi (entry->d_name);
      snprintf (procdir, sizeof (procdir), "/proc/%d", pid);
      stat (procdir, &statbuf);
      if (statbuf.st_uid > 0)
	pidlist_pids[pidlist_count++] = pid;
      if (pidlist_count >= PIDLIST_SIZE)
	break;
    }

  closedir (dir);
}

int
main (int argc, char **argv)
{
  double target_cpu_usage = CPU_T1MICRO;

  if (geteuid () != 0)
    mm_fatal ("must run as root");

  if (sysconf (_SC_NPROCESSORS_ONLN) != 1)
    mm_fatal ("able to manage only one CPU, SMP not supported");

  setpriority (PRIO_PROCESS, 0, -10);

  signal (SIGINT, signal_handler);
  signal (SIGQUIT, signal_handler);
  signal (SIGTERM, signal_handler);

  while (running)
    {
      int i;
      struct timespec time_running, time_sleeping;

      target_cpu_usage = target_cpu_usage / get_cpu_usage () * CPU_T1MICRO;
      if (target_cpu_usage > CPU_MAX)
	target_cpu_usage = CPU_MAX;
      if (target_cpu_usage < CPU_MIN)
	target_cpu_usage = CPU_MIN;

      time_running.tv_sec = 0;
      time_running.tv_nsec = SCHED_QUANTUM_NS * target_cpu_usage;

      time_sleeping.tv_sec = 0;
      time_sleeping.tv_nsec = SCHED_QUANTUM_NS - time_running.tv_nsec;

      update_pidlist ();

      /* force non-root processes to sleep */
      for (i = 0; i < pidlist_count; i++)
	kill (pidlist_pids[i], SIGSTOP);
      nanosleep (&time_sleeping, NULL);

      /* allow processes to run normally */
      for (i = 0; i < pidlist_count; i++)
	kill (pidlist_pids[i], SIGCONT);
      nanosleep (&time_running, NULL);
    }

  exit (EXIT_SUCCESS);
}
