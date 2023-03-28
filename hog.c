/* ================================================================= */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <sched.h>

#include <sys/times.h>

/* ================================================================= */

const long cOneMbyte = 1024 * 1024;

const int TRUE = 1;
const int FALSE = 0;

/* ================================================================= */

void analyseCPUs()
{
  FILE *fd = popen("grep 'cpu cores' /proc/cpuinfo | uniq | sed -e's/^.*: //'", "r");
  int numCores;
  fscanf(fd, "%d", &numCores);
  pclose(fd);

  fd = popen("grep 'cpu cores' /proc/cpuinfo | wc -l", "r");
  int numThreads;
  fscanf(fd, "%d", &numThreads);
  pclose(fd);

  printf("Number of CPU cores: %d, threads: %d\n", numCores, numThreads);
}

/* ================================================================= */

// #define DEBUG

#ifdef DEBUG
#define DEBUG_FREQUENCY 1000
static int sDebugNow = DEBUG_FREQUENCY;
#endif

/* ================================================================= */

void adjustLoad(clock_t startTime, int load)
{
  static time_t sUsleepTime = 0;
  static clock_t sLastCPUTimeUsed = 0;
  static double sWrappedCPUTime = 0.0;

  struct tms _tms;
  clock_t currentTime;
  clock_t elapsedTime;
  clock_t CPUTimeUsed;
  double totalCPUTimeUsed;
  int currentLoad;

  currentTime = times(&_tms);

  elapsedTime = currentTime - startTime;
  elapsedTime = elapsedTime * 10; /* Now in msecs */

  CPUTimeUsed = clock() / 1000;

  if (CPUTimeUsed < sLastCPUTimeUsed)
  {
    /* clock() has wrapped */
    sWrappedCPUTime += 4295000;
  }
  sLastCPUTimeUsed = CPUTimeUsed;

  totalCPUTimeUsed = sWrappedCPUTime + CPUTimeUsed;

  currentLoad = (int)(100.5 * totalCPUTimeUsed / (double)elapsedTime);

  if (currentLoad >= load)
  {
    sUsleepTime = sUsleepTime * 2;

    if (sUsleepTime < 10)
    {
      sUsleepTime = 10;
    }

    if (sUsleepTime >= 1000000)
    {
      sUsleepTime = 1000000;
    }
  }
  else
  {
    sUsleepTime = 0;
  }

  if (sUsleepTime != 0)
  {
    // printf("Sleeping %d us\n", sUsleepTime);
    (void)usleep(sUsleepTime);
  }

#ifdef DEBUG
  if (--sDebugNow == 0)
  {
    printf("DEBUG> elapsed = %.02f s, cpu = %.02f s, load = %d",
           elapsedTime / 1000.0,
           totalCPUTimeUsed / 1000.0,
           currentLoad);

    printf(" => sleep = %d\n", sUsleepTime);

    sDebugNow = DEBUG_FREQUENCY;
  }
#endif
}

/* ================================================================= */

// Track number of grabs
static long sGrabCycle = 0;

// Hold onto the previosly grabbed memory for a cycle
static char *spPrevGrabbedMemory = 0;

static char *spGrabbedMemory = 0;
static char *spGrabbedMemoryStart = 0;
static char *spGrabbedMemoryEnd = 0;
static char *spGrabbedMemoryCurrent = 0;

/* ================================================================= */

int grabMemory(amount)
long amount;
{
  sGrabCycle++;

  int returnValue = 0;

  if (amount > 0)
  {
    printf("\nGrabbing %d Mbytes of memory [#%d]...\n", amount / cOneMbyte, sGrabCycle);

    if (spPrevGrabbedMemory != 0)
    {
      free(spPrevGrabbedMemory);
    }

    spPrevGrabbedMemory = spGrabbedMemory;
    spGrabbedMemory = (char *)malloc(amount);

    if (spGrabbedMemory == 0)
    {
      perror("Unable to grab requested memory");

      returnValue = errno;
    }
    else
    {
      spGrabbedMemoryStart = spGrabbedMemory;
      spGrabbedMemoryEnd = spGrabbedMemory + amount - 1;
      spGrabbedMemoryCurrent = spGrabbedMemory;
    }
  }

  return returnValue;
}

/* ================================================================= */

const char *SPINNER_BODY = ":-";
const char *DEFAULT_SPINNER = "o)";

// After how many full memory update cyles do we regrab
const int REGRAB_START = 20;
static int regrab = REGRAB_START;

// We don't need to set all bytes to make it resident
const int MEM_STRIDE = 64;
// Adjust this many bytes at a time - too high reduces load tracking accuracy
const int STRIDES_PER_CALL = 10;

void useMemory(long amount, const char *spinner)
{
  static int spinPos = 0;

  if (spGrabbedMemory != 0)
  {
    if (spGrabbedMemoryCurrent == spGrabbedMemoryStart && strcmp(spinner, "NOSPINNER") != 0)
    {
      printf("\r%s%c", SPINNER_BODY, spinner[spinPos++]);
      fflush(stdout);

      if (spinner[spinPos] == '\0')
        spinPos = 0;
    }

    for (int i = 0; i < STRIDES_PER_CALL; i++)
    {
      *spGrabbedMemoryCurrent = 0xAA;

      spGrabbedMemoryCurrent += MEM_STRIDE;

      if (spGrabbedMemoryCurrent > spGrabbedMemoryEnd)
      {
        regrab--;
        if (regrab == 0)
        {
          // grabMemory(amount);
          regrab = REGRAB_START;
        }

        spGrabbedMemoryCurrent = spGrabbedMemoryStart;

        // Start from beginning next time...
        break;
      }
    }
  }
}

/* ================================================================= */

const int NO_CPU = -1;

void pinCpu(int cpu)
{
  if (cpu == NO_CPU)
    return;

  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);

  CPU_SET(cpu, &cpuset);

  pid_t pid = getpid();

  int res = sched_setaffinity(pid, sizeof(cpuset), &cpuset);

  if (res == -1)
  {
    printf("ERROR: Unable to pin to CPU %d\n", cpu);
  }
  else
  {
    printf("OK: Pinned to CPU %d\n", cpu);
  }
}

/* ================================================================= */

void showHelp()
{
  static char *help =
      "hog\n"
      "\n"
      "Memory, CPU and CPU load hog utility.\n"
      "\n"
      "I want to hog ...\n"
      "\n"
      "A CPU - Effectively disabling one on a multi CPU machine\n"
      "\n"
      "./hog 0 <CPU>  -- <CPU> is the number of the CPU to TOTALLY hog\n"
      "\n"
      "And some memory while I'm at it\n"
      "\n"
      "./hog <MEM> <CPU>  -- <MEM> is the number of megabytes to hog <CPU> as above\n"
      "\n"
      "Memory and no CPU\n"
      "\n"
      "./hog <MEM> 0 0  -- <MEM> is the number of megabytes to hog\n"
      "\n"
      "This is a tricky one, as hogging memory really requires some\n"
      "CPU to hog effectively. Using <CPU> = 0 will result in the\n"
      "time taken to hog the memory being quite long and may also\n"
      "result in some memory being swapped out as a result\n"
      "\n"
      "Memory and some CPU\n"
      "\n"
      "./hog <MEM> 0 <LOAD>  -- <MEM> as above and <LOAD> is the %% load on the CPU\n"
      "which in this case is 0 - use this for a single CPU\n"
      "machine.\n"
      "\n"
      "./hog <MEM> <CPU> <LOAD>  -- as above except choose <CPU> too\n"
      "\n"
      "Just some CPU\n"
      "\n"
      "./hog 0 <CPU> <LOAD>  -- just set <MEM> to 0\n"
      "\n"
      "\n"
      "Resonable Values\n"
      "\n"
      "<MEM>	Up to the max allowed for your process and within sensible\n"
      "limits for the machine - asking for too much will lead to\n"
      "paging/swapping\n"
      "\n"
      "<CPU>	Depends on number of CPUs on your machine\n"
      "\n"
      "<LOAD>	Low values, less than 5%%, are hard to achieve\n"
      "\n";

  printf(help);
}

/* ================================================================= */

const char *OPTIONS[] = {
    "-help",
    "help"};

int isAnyOf(char *str, const char *options[])
{
  int res = FALSE;

  for (int i = 0; i < sizeof(OPTIONS) / sizeof(char *); i++)
  {
    res = strcasecmp(str, options[i]) == 0;
    if (res)
      break;
  }

  return res;
}

void showHelpIfNeeded(int argc, char *argv[])
{
  if ((argc == 1) || ((argc >= 1) && (isAnyOf(argv[1], OPTIONS))))
  {
    showHelp();

    analyseCPUs();

    exit(0);
  }
}

/* ================================================================= */

const long ONE_MB = 1024 * 1024;

long megabytesToBytes(int mb)
{
  return mb * ONE_MB;
}

double bytesToMegabytes(long bytes)
{
  return (bytes * 1.0) / ONE_MB;
}

/* ================================================================= */

const long DEFAULT_MEMORY = 8 * ONE_MB;
const int DEFAULT_CPU = NO_CPU;
const int DEFAULT_LOAD = 50;
const int FULL_LOAD = 100;

typedef struct
{
  long memory;
  int cpu;
  int load;
  const char *spinner;
  clock_t startTime;
} Settings;

void parseArgs(int argc, char *argv[], Settings *settings)
{
  settings->memory = DEFAULT_MEMORY;
  settings->cpu = DEFAULT_CPU;
  settings->load = DEFAULT_LOAD;
  settings->spinner = DEFAULT_SPINNER;

  struct tms _tms;
  settings->startTime = times(&_tms);

  showHelpIfNeeded(argc, argv);

  if (argc >= 2)
  {
    settings->memory = megabytesToBytes(atoi(argv[1]));
  }

  if (argc >= 3)
  {
    settings->cpu = atoi(argv[2]);
    settings->load = FULL_LOAD;
  }

  if (argc >= 4)
  {
    settings->load = atoi(argv[3]);
  }

  if (argc == 5)
  {
    settings->spinner = argv[4];
  }
}

/* ================================================================= */

void loadRunner(Settings *settings)
{

  int returnValue = grabMemory(settings->memory);

  if (returnValue != 0)
  {
    exit(returnValue);
  }

  int i = 0;
  int adjustFrequency = ((settings->load / 5) + 1) * 4096;

  pinCpu(settings->cpu);

  printf("\nPress Ctrl-C to exit ...\n");
  fflush(stdout);

  for (;;)
  {
    useMemory(settings->memory, settings->spinner);

    i++;

    if (settings->load < 100)
    {
      if ((i % adjustFrequency == 0))
      {
        i = 0;

        adjustLoad(settings->startTime, settings->load);
      }
    }
  }
}

/* ================================================================= */

int main(argc, argv)
int argc;
char *argv[];
{
  Settings settings;

  parseArgs(argc, argv, &settings);

  printf("\nmemory = %4.0lfMB, cpu = %d, load = %d, spinner = '%s'\n",
         bytesToMegabytes(settings.memory),
         settings.cpu,
         settings.load,
         settings.spinner);

  loadRunner(&settings);
}

/* ================================================================= */
