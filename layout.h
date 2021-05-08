#ifndef LAYOUT_H
#define LAYOUT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <math.h>
#include <semaphore.h>
#include <sys/ipc.h> 
#include <sys/msg.h>
#include <errno.h>

#define NUM_PCBS 18

struct message {
	long type;
	char text[16];
};

/*

Design and implement a memory management module for our Operating System Simulator oss.
In particular, we will implement the second-chance (clock) page replacement algorithm. However, processes will implement their memory accesses in two different ways. Your program should take in a command line option -m x, which accepts either a 0 or a 1, which determines how child processes will perform their memory access.
When a page-fault occurs, it will be necessary to swap in that page. If there are no empty frames, your algorithm will select the victim frame based on the clock algorithm.
Each frame should also have an additional dirty bit, which is set on writing to the frame. This bit is necessary to consider dirty bit optimization to determine how much time these operations take. The dirty bit is implemented as a part of the page table.
Operating System Simulator
This will be your main program and serve as the master process. You will start the operating system simulator (call the executable oss) as one main process who will fork multiple children at random times. The randomness will be simulated by a logical clock that will be updated by oss as well as user processes. Thus, the logical clock resides in shared memory and is accessed as a critical resource using a semaphore. You should have two unsigned integers for the clock; one will show the time in seconds and the other will show the time in nanoseconds, offset from the beginning of a second.
In the beginning, oss will allocate shared memory for system data structures, including page table. You can create fixed sized arrays for page tables, assuming that each process will have a requirement of less than 32K memory, with each page being 1K. The page table should also have a delimiter indicating its size so that your programs do not access memory beyond the page table limit. The page table should have all the required fields that may be implemented by bits or character data types.
Assume that your system has a total memory of 256K. You will require a frame table, with data required such as reference bit and dirty bit. Use a bit vector to keep track of the unallocated frames.
After the resources have been set up, fork a user process at random times (between 1 and 500 milliseconds of your logical clock). Make sure that you never have more than 18 user processes in the system. If you already have 18 processes, do not create any more until some process terminates. 18 processes is a hard limit and you should implement it using a #define macro. Thus, if a user specifies an actual number of processes as 30, your hard limit will still limit it to no more than 18 processes at any time in the system. Your user processes execute concurrently and there is no scheduling performed. They run in a loop constantly till they have to terminate.


oss will monitor all memory references from user processes and if the reference results in a page fault, the process will be suspended till the page has been brought in. It is up to you how you do synchronization for this project, so for example you could use message queues or a semaphore for each process. Effectively, if there is no page fault, oss just increments the clock by 10 nanoseconds and sends a signal on the corresponding semaphore. In case of page fault, oss queues the request to the device. Each request for disk read/write takes about 14ms to be fulfilled. In case of page fault, the request is queued for the device and the process is suspended as no signal is sent on the semaphore. The request at the head of the queue is fulfilled once the clock has advanced by disk read/write time since the time the request was found at the head of the queue. The fulfillment of request is indicated by showing the page in memory in the page table. oss should periodically check if all the processes are queued for device and if so, advance the clock to fulfill the request at the head. We need to do this to resolve any possible deadlock in case memory is low and all processes end up waiting.
Memory Management 2
While a page is referenced, oss performs other tasks on the page table as well such as updating the page reference, setting up dirty bit, checking if the memory reference is valid and whether the process has appropriate permissions on the frame, and so on.
When a process terminates, oss should log its termination in the log file and also indicate its effective memory access time. oss should also print its memory map every second showing the allocation of frames. You can display unallocated frames by a period and allocated frame by a +.
For example at least something like...
Master: P2 requesting read of address 25237 at time xxx:xxx
Master: Address 25237 in frame 13, giving data to P2 at time xxx:xxx
Master: P5 requesting write of address 12345 at time xxx:xxx
Master: Address 12345 in frame 203, writing data to frame at time xxx:xxx
Master: P2 requesting write of address 03456 at time xxx:xxx
Master: Address 12345 is not in a frame, pagefault
Master: Clearing frame 107 and swapping in p2 page 3
Master: Dirty bit of frame 107 set, adding additional time to the clock
Master: Indicating to P2 that write has happened to address 03456
Current memory layout at time xxx:xxx is:
         Occupied   RefByte  DirtyBit
Frame 0: No
Frame 1: Yes
Frame 2: Yes
Frame 3: Yes
0 0 13 1 1 0 120 1
where Occupied indicates if we have a page in that frame, the refByte is the value of our reference bits in the frame and the dirty bit indicates if the frame has been written into.
User Processes
Each user process generates memory references to one of its locations. This should be done in two different ways. In particular, your project should take a command line argument (-m x) where x specifies which of the following memory request schemes is used in the child processes:
• The first memory request scheme is simple. When a process needs to generate an address to request, it simply generates a random value from 0 to the limit of the process memory (32k).
• The second memory request scheme tries to favor certain pages over others. You should implement a scheme where each of the 32 pages of a process’ memory space has a different weight of being selected. In particular, the weight of a processes page n should be 1/n. So the first page of a process would have a weight of 1, the second page a weight of 0.5, the third page a weight of 0.3333 and so on. Your code then needs to select one of those pages based on these weights. This can be done by storing those values in an array and then, starting from the beginning, adding to each index of the array the value in the preceding index. For example, if our weights started off as [1,0.5,0.3333,0.25,...] then after doing this process we would get an array of [1, 1.5, 1.8333, 2.0833, . . .]. Then you generate a random number from 0 to the last value in the array and then travel down the array until you find a value greater than that value and that index is the page you should request.
Now you have the page of the request, but you still need the offset. Multiply that page number by 1024 and then add a random offset of from 0 to 1023 to get the actual memory address requested.
Once this is done, you now have a memory address, but you still must determine if it is a read or write. Do this with randomness, but bias it towards reads. This information (the address requested and whether it is a read or write)

Memory Management 3
should be conveyed to oss. The user process will wait on its semaphore (or message queue if implemented that way) that will be signaled by oss. oss checks the page reference by extracting the page number from the address, increments the clock as specified above, and sends a signal on the semaphore if the page is valid.
At random times, say every 1000 ± 100 memory references, the user process will check whether it should terminate. If so, all its memory should be returned to oss and oss should be informed of its termination.
The statistics of interest are:
• Number of memory accesses per second
• Number of page faults per memory access • Average memory access speed
You should terminate after more than 100 processes have gotten into your system, or if more than 2 real life seconds have passed. Make sure that you have signal handling to terminate all processes, if needed. In case of abnormal termination, make sure to remove shared memory, queues, and semaphores.
In your README file, discuss the performance of the page replacement algorithm on both the page request schemes. Which one is a more realistic model of the way pages would actually be requested?
I suggest you implement these requirements in the following order:
1. Get a Makefile that compiles two source files, have oss allocate shared memory, use it, then deallocate it. Make sure to check all possible error returns.
2. Get oss to fork and exec one child and have that child attach to shared memory and check the clock and verify it has correct resource limit. Then test having child and oss communicate through message queues. Set up pcb and frame table/page tables
3. Have child request a read/write of a memory address (using the first scheme) and have oss always grant it and log it.
4. Set up more than one process going through your system, still granting all requests.
5. Now start filling out your page table and frame table; if a frame is full, just empty it (indicating in the process that you took it from is gone) and granting request.
6. Implement a wait queue for i/o delay on needing to swap out a process.
7. Do not forget that swapping out a process with a dirty bit should take more time on your device
8. Implement the clock replacement policy.
9. Implement the 1/n weight request scheme.
Deliverables
Handin an electronic copy of all the sources, README, Makefile(s), and results. Create your programs in a directory called username.6 where username is your login name on hoare. Once you are done with everything, remove the executables and object files, and issue the following commands:
% cd
% chmod 755 ~
% ~sanjiv/bin/handin cs4760 6
% chmod 700 ~
*/


struct frame {
	int pid;
	int active;
	int addr;
	int secondchance;
	int dirty;
};

struct ptab {
	struct frame pages[32];
};

struct shm_layout {
	unsigned int seconds, nanoseconds;
	struct ptab page_tables[NUM_PCBS];
	struct frame frame_table[256];
	pid_t realpids[NUM_PCBS];
	int waiting[NUM_PCBS];
	int mode;
};

void add_times (unsigned int *s, unsigned int *ns, unsigned int addn);

#endif