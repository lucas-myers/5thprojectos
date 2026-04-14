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

This step was mainly focused on preparing the system to support resource requests.

AI used: ChatGPT

Prompts:

 How should I structure resource management for an operating system simulation?

 What fields should be added to a PCB for resource tracking?

 How do I initialize arrays inside a struct in C++?

How should I design a resource table for multiple processes?

Summary:
