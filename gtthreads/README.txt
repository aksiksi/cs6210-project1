## GTThreads Library

A Unix threading library built on top of processes via `clone(2)`. The library implements both a priority scheduler and a Xen-inspired credit scheduler.

**Warning:** this library neither compiles nor runs on OS X!

### Build

Navigate to `gtthreads/`, then run `make`.

To build the sample matrix multiplication application, run `make matrix`.

Both binaries will be located under `bin/`.

### Usage

The library itself -- `libuthread.a` -- can be linked in during compilation.

The matrix app takes in a single argumnent: 0 for priority scheduler, and 1 for credit scheduler.
