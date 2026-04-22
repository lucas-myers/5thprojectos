Name: Lucas Myers
Date: 4/13/2026
Environment: Visual Studio Code, Linux

How to Compile Project:
Type 'make'

Example of how to run the project:

From the project directory:

make clean
make
./oss -n 5 -s 2 -t 3 -i 0.5



1. dUsed my previous project (Project 4) as a base.

2. Added new constants to both oss and worker to represent the system resources. The system now has 10 resource classes with 5 instances each.

3. Modified the PCB structure to support resource tracking by adding an array to store resources allocated to each process and a variable to track the current requested resource.

4. Updated the process table initialization function to properly initialize all fields that require resourches. Each process now starts with zero and no active request.

5. Added a global resource table in oss to track the number of available instances for each resource in the system.

6. Initialized all resources at the start of the program so each resource class begins with the maximum number of instances available.

7. Updated the process removal function to clear all resource data when a process terminates to prevent leftover data from affecting future processes.

8. Modified the worker launch function to ensure all resource tracking fields are reset when a new process is created.

9. At this stage, the program successfully compiles and runs with the new data structures in place. 

10. Replaced the old worker CPU burst and quantum behavior with resource management behavior.

11. Updated the worker so it waits for a message from oss and then randomly decides whether to request a resource, release a resource, or terminate.

12. Changed the message structure so workers now communicate resource actions instead of time quantum usage.

13. Modified oss so it launches the worker with only its process table index instead of burst time arguments.

14. This step sets up the communication format needed for resource allocation and deadlock handling in the next stage.

15. Added the deadlock detection files provided and updated the project build so oss links against the deadlock detection implementation.

16. Built helper logic in oss to generate the request and allocation matrices needed by the deadlock detection algorithm.

17. Added a periodic deadlock detection check that runs every simulated second.

18. Updated logging and final statistics so the program now reports how many times deadlock detection was run and whether a deadlock was found.

19. Ran into compile Error switching computers. Giving up going to try to fix later.



AI used: ChatGPT

Prompts:

 How should I structure resource management for an operating system simulation?

 What fields should be added to a PCB for resource tracking?

 How do I initialize arrays inside a struct in C++?

How should I design a resource table for multiple processes?

How should processes request and release resources in an operating system simulation?

How should blocking and unblocking work for resource allocation?

Summary:
