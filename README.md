# Simple Operating System

As a major part of the [COMP9242](https://cgi.cse.unsw.edu.au/~cs9242/current/) course at UNSW, we need to build some essential components of an operating system on top of the [seL4 microkernel](https://sel4.systems/). These includes:
- A timer driver
- A system call interface
- A virtual memory manager
- A virtual file system, via interactions with a network file system through [libnfs](https://github.com/sahlberg/libnfs)
- Demand paging 
- Process management
- And, `mmap()` and `munmap()` system calls as a bonus feature.

For specific requirements of the above features, please refer to the [course website](https://cgi.cse.unsw.edu.au/~cs9242/25/project/index.shtml).

We were also fortunate enough to be the winner of the file system benchmark, out of all the thread-based OS personalities (7,300 KB/s).

For implementation details, please refer to our [system documentation](AOS_SystemDocumentation_Group19.pdf). 