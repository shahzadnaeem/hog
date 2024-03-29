# hog

Memory, CPU and CPU load hog utility.

I want to hog ...

## A CPU - Effectively disabling one on a multi CPU machine

./hog 0 `CPU`  -- `CPU` is the number of the CPU to TOTALLY hog

  And some memory while I'm at it

  ./hog `MEM` `CPU`  -- `MEM` is the number of megabytes to hog `CPU` as above

## Memory and no CPU

./hog `MEM` 0 0  -- `MEM` is the number of megabytes to hog

This is a tricky one, as hogging memory really requires some
CPU to hog effectively. Using `CPU` = 0 will result in the
time taken to hog the memory being quite long and may also
result in some memory being swapped out as a result

## Memory and some CPU

./hog `MEM` 0 `LOAD`  -- `MEM` as above and `LOAD` is the % load on the CPU
                         which in this case is 0 - use this for a single CPU
                         machine.

./hog `MEM` `CPU` `LOAD`  -- as above except choose `CPU` too

Just some CPU

./hog 0 `CPU` `LOAD`  -- just set `MEM` to 0

## Help

./hog       -- displays help

./hog cpus  -- display number of cores and threads on current machine

## Resonable Values

`MEM` Up to the max allowed for your process and within sensible
limits for the machine - asking for too much will lead to
paging/swapping

`CPU` Depends on number of CPUs on your machine

`LOAD` Low values, less than 5%, are hard to achieve

`SPINNER` Extra parameter to use as spinner chars - "o)" is default
          Try "/-\\|/-" for an actual spinner
