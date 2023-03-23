/* ================================================================= */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <malloc.h>
#include <string.h>
#include <time.h>

#include <sched.h>

#include <sys/times.h>

/* ================================================================= */

const long cOneMbyte = 1024 * 1024;

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

static struct tms sDummyTms;
static clock_t sStartTime;
static int sTargetLoad;
static time_t sUsleepTime = 0;
static clock_t sLastCPUTimeUsed = 0;
static double sWrappedCPUTime = 0.0;

#ifdef DEBUG
#define DEBUG_FREQUENCY 1000
static int sDebugNow = DEBUG_FREQUENCY;
#endif

/* ================================================================= */

void adjustLoad()
{
  clock_t currentTime;
  clock_t elapsedTime;
  clock_t CPUTimeUsed;
  double totalCPUTimeUsed;
  int currentLoad;

  currentTime = times(&sDummyTms);

  elapsedTime = currentTime - sStartTime;
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

  if (currentLoad >= sTargetLoad)
  {
    sUsleepTime = sUsleepTime * 2 + sTargetLoad;

    if (sUsleepTime > 999999)
    {
      sUsleepTime = 999999;
    }
  }
  else
  {
    sUsleepTime = 0;
  }

  if (sUsleepTime != 0)
  {
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
  int returnValue = 0;

  if (amount > 0)
  {
    printf("\nGrabbing %d Mbytes of memory ...\n", amount / cOneMbyte);

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

// Spinner chars to display
static int sSpinPos = 0;
static char *sSpinner = "o)";

// After how many full memory update cyles do we regrab
const int REGRAB_START = 20;
static int regrab = REGRAB_START;

// We don't need to set all bytes to make it resident
const int MEM_STRIDE = 64;

void useMemory(long amount, const char *spinner)
{
  if (spGrabbedMemory != 0)
  {
    *spGrabbedMemoryCurrent = 0xAA;

    spGrabbedMemoryCurrent += MEM_STRIDE;

    if (spGrabbedMemoryCurrent > spGrabbedMemoryEnd)
    {
      if (strcmp(sSpinner, "NOSPINNER") != 0)
      {
        printf("\r%.*s%c", strlen(spinner) - 1, spinner, sSpinner[sSpinPos++]);
        fflush(stdout);

        if (sSpinner[sSpinPos] == '\0')
          sSpinPos = 0;
      }

      regrab--;
      if (regrab == 0)
      {
        grabMemory(amount);
        regrab = REGRAB_START;
      }

      spGrabbedMemoryCurrent = spGrabbedMemoryStart;
    }
  }
}

/* ================================================================= */

void pinCpu(int cpu)
{
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
    printf("OK: Pinned to pin to CPU %d\n", cpu);
  }
}

/* ================================================================= */

void showHelp()
{
  static char *help =
      "hog\n"
      "===\n"
      "\n"
      "Memory, CPU and CPU load hog utility.\n"
      "\n"
      "I want to hog ...\n"
      "\n"
      "A CPU - Effectively disabling one on a multi CPU machine\n"
      "========================================================\n"
      "\n"
      "./hog 0 <CPU>  -- <CPU> is the number of the CPU to TOTALLY hog\n"
      "\n"
      "And some memory while I'm at it\n"
      "===============================\n"
      "\n"
      "./hog <MEM> <CPU>  -- <MEM> is the number of megabytes to hog <CPU> as above\n"
      "\n"
      "Memory and no CPU\n"
      "=================\n"
      "\n"
      "./hog <MEM> 0 0  -- <MEM> is the number of megabytes to hog\n"
      "\n"
      "This is a tricky one, as hogging memory really requires some\n"
      "CPU to hog effectively. Using <CPU> = 0 will result in the\n"
      "time taken to hog the memory being quite long and may also\n"
      "result in some memory being swapped out as a result\n"
      "\n"
      "Memory and some CPU\n"
      "===================\n"
      "\n"
      "./hog <MEM> 0 <LOAD>  -- <MEM> as above and <LOAD> is the %% load on the CPU\n"
      "which in this case is 0 - use this for a single CPU\n"
      "machine.\n"
      "\n"
      "./hog <MEM> <CPU> <LOAD>  -- as above except choose <CPU> too\n"
      "\n"
      "Just some CPU\n"
      "=============\n"
      "\n"
      "./hog 0 <CPU> <LOAD>  -- just set <MEM> to 0\n"
      "\n"
      "\n"
      "Resonable Values\n"
      "================\n"
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

int main(argc, argv, envp)
int argc;
char *argv[];
char *envp[];
{
  long memory = 8 * cOneMbyte;
  int cpu = 0;
  int returnValue = 0;

  sTargetLoad = 50;

  if ((argc == 1) || ((argc >= 1) && ((argv[1][0] == '-') || (argv[1][0] == 'h') || (argv[1][0] == 'H'))))
  {
    showHelp();

    analyseCPUs();

    exit(0);
  }

  if (argc >= 2)
  {
    memory = atoi(argv[1]) * cOneMbyte;
  }

  if (argc >= 3)
  {
    cpu = atoi(argv[2]);
    sTargetLoad = 100;
  }

  if (argc >= 4)
  {
    sTargetLoad = atoi(argv[3]);
  }

  if (argc == 5)
  {
    sSpinner = argv[4];
  }

  printf("\nmemory = %d, cpu = %d, load = %d\n",
         memory,
         cpu,
         sTargetLoad);

  returnValue = grabMemory(memory);

  if (returnValue != 0)
  {
    exit(returnValue);
  }

  sStartTime = times(&sDummyTms);

  int i = 0;
  int adjustFrequency = ((sTargetLoad / 5) + 1) * 4096;

  pinCpu(cpu);

  const char *spinner = ":-)";

  printf("\nPress Ctrl-C to exit ...\n%s", spinner);
  fflush(stdout);

  for (;;)
  {
    useMemory(memory, spinner);

    i++;

    if (sTargetLoad < 100)
    {
      if ((i % adjustFrequency == 0))
      {
        i = 0;

        adjustLoad();
      }
    }
  }
}

/* ================================================================= */
