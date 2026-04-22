#include <iostream>
#include <fstream>
#include <queue>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include "deadlockdetection.h"

using namespace std;

const int MAX_TOTAL_PROCESSES = 20;
const int PCB_SIZE = 18;
const int NUM_RESOURCES = 10;
const int MAX_INSTANCES = 5;
const unsigned int BILLION = 1000000000;
const unsigned int CLOCK_INCREMENT = 10000000;   // 10 ms
const unsigned int IDLE_INCREMENT = 100000;      // 100 microseconds
const unsigned int DISPATCH_OVERHEAD = 1000;
const int MAX_LOG_LINES = 10000;

struct SimClock {
    unsigned int seconds;
    unsigned int nanoseconds;
};

struct PCB {
    int occupied;
    pid_t pid;
    int localPid;
    unsigned int startSeconds;
    unsigned int startNano;
    unsigned int serviceTimeSeconds;
    unsigned int serviceTimeNano;
    unsigned int eventWaitSec;
    unsigned int eventWaitNano;
    int blocked;

    int resourcesAllocated[NUM_RESOURCES];
    int requestedResource;
};

struct Message {
    long mtype;
    int index;
    int resource;
};

int shmId = -1;
int msgId = -1;
SimClock* simClock = nullptr;

PCB processTable[PCB_SIZE];
queue<int> readyQueue;

int availableResources[NUM_RESOURCES];

int totalChildren = 5;
int maxSimultaneous = 2;
double timeLimitForChildren = 3.0;
double launchInterval = 0.5;
string logFileName = "oss.log";

ofstream logFile;
int logLines = 0;

int launchedTotal = 0;
int finishedTotal = 0;
int runningNow = 0;

unsigned int nextLaunchSec = 0;
unsigned int nextLaunchNano = 0;
unsigned int nextTablePrintSec = 0;
unsigned int nextTablePrintNano = 500000000;

unsigned long long totalRequests = 0;
unsigned long long grantedImmediately = 0;
unsigned long long totalReleases = 0;

unsigned int nextDeadlockCheckSec = 1;
unsigned int nextDeadlockCheckNano = 0;

unsigned long long deadlockRuns = 0;
unsigned long long deadlocksFound = 0;

void buildDeadlockMatrices(int requestMatrix[], int allocatedMatrix[]) {
    for (int i = 0; i < PCB_SIZE; i++) {
        for (int j = 0; j < NUM_RESOURCES; j++) {
            requestMatrix[i * NUM_RESOURCES + j] = 0;
            allocatedMatrix[i * NUM_RESOURCES + j] = 0;
        }
    }

    for (int i = 0; i < PCB_SIZE; i++) {
        if (!processTable[i].occupied) {
            continue;
        }

        for (int j = 0; j < NUM_RESOURCES; j++) {
            allocatedMatrix[i * NUM_RESOURCES + j] = processTable[i].resourcesAllocated[j];
        }

        if (processTable[i].blocked &&
            processTable[i].requestedResource >= 0 &&
            processTable[i].requestedResource < NUM_RESOURCES) {
            requestMatrix[i * NUM_RESOURCES + processTable[i].requestedResource] = 1;
        }
    }
}

void runDeadlockDetection() {
    int requestMatrix[PCB_SIZE * NUM_RESOURCES];
    int allocatedMatrix[PCB_SIZE * NUM_RESOURCES];

    buildDeadlockMatrices(requestMatrix, allocatedMatrix);

    writeLog("OSS: Running deadlock detection at time " +
             to_string(simClock->seconds) + ":" +
             to_string(simClock->nanoseconds) + "\n");

    deadlockRuns++;

    int isDeadlocked = deadlock(availableResources,
                                NUM_RESOURCES,
                                PCB_SIZE,
                                requestMatrix,
                                allocatedMatrix);

    if (isDeadlocked) {
        deadlocksFound++;
        writeLog("OSS: Deadlock detected.\n");
    } else {
        writeLog("OSS: No deadlock detected.\n");
    }
}

void writeLog(const string& text) {
    cout << text;

    if (logLines < MAX_LOG_LINES) {
        logFile << text;
        logLines++;
        if (logLines == MAX_LOG_LINES) {
            logFile << "OSS: Log limit reached, suppressing further log output.\n";
        }
    }
}

void addToTime(unsigned int& sec, unsigned int& nano, unsigned int addNano) {
    nano += addNano;
    while (nano >= BILLION) {
        nano -= BILLION;
        sec++;
    }
}

void advanceClock(unsigned int ns) {
    addToTime(simClock->seconds, simClock->nanoseconds, ns);
}

bool timeReached(unsigned int sec1, unsigned int nano1,
                 unsigned int sec2, unsigned int nano2) {
    if (sec1 > sec2) return true;
    if (sec1 == sec2 && nano1 >= nano2) return true;
    return false;
}

void cleanup() {
    if (simClock != nullptr) {
        shmdt(simClock);
        simClock = nullptr;
    }

    if (shmId != -1) {
        shmctl(shmId, IPC_RMID, nullptr);
        shmId = -1;
    }

    if (msgId != -1) {
        msgctl(msgId, IPC_RMID, nullptr);
        msgId = -1;
    }
}

void killChildren() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].pid > 0) {
            kill(processTable[i].pid, SIGTERM);
        }
    }

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

void signalHandler(int sig) {
    cerr << "\nOSS: caught signal " << sig << ", cleaning up.\n";
    killChildren();
    cleanup();
    exit(1);
}

void printUsage(const char* program) {
    cout << "Usage: " << program
         << " [-h] [-n proc] [-s simul] [-t timelimitForChildren] "
         << "[-i fractionOfSecondToLaunchChildren] [-f logfile]\n";
}

void parseArguments(int argc, char* argv[]) {
    int opt;

    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printUsage(argv[0]);
                exit(0);
            case 'n':
                totalChildren = atoi(optarg);
                break;
            case 's':
                maxSimultaneous = atoi(optarg);
                break;
            case 't':
                timeLimitForChildren = atof(optarg);
                break;
            case 'i':
                launchInterval = atof(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                printUsage(argv[0]);
                exit(1);
        }
    }

    if (totalChildren < 1) totalChildren = 1;
    if (totalChildren > MAX_TOTAL_PROCESSES) totalChildren = MAX_TOTAL_PROCESSES;

    if (maxSimultaneous < 1) maxSimultaneous = 1;
    if (maxSimultaneous > PCB_SIZE) maxSimultaneous = PCB_SIZE;

    if (timeLimitForChildren <= 0) timeLimitForChildren = 1.0;
    if (launchInterval < 0) launchInterval = 0.0;
}

void initProcessTable() {
    for (int i = 0; i < PCB_SIZE; i++) {
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].localPid = i;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].serviceTimeSeconds = 0;
        processTable[i].serviceTimeNano = 0;
        processTable[i].eventWaitSec = 0;
        processTable[i].eventWaitNano = 0;
        processTable[i].blocked = 0;
        processTable[i].requestedResource = -1;

        for (int j = 0; j < NUM_RESOURCES; j++) {
            processTable[i].resourcesAllocated[j] = 0;
        }
    }
}

int getFreePCB() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (!processTable[i].occupied) {
            return i;
        }
    }
    return -1;
}

void removeFromPCB(int index) {
    processTable[index].occupied = 0;
    processTable[index].pid = 0;
    processTable[index].localPid = index;
    processTable[index].startSeconds = 0;
    processTable[index].startNano = 0;
    processTable[index].serviceTimeSeconds = 0;
    processTable[index].serviceTimeNano = 0;
    processTable[index].eventWaitSec = 0;
    processTable[index].eventWaitNano = 0;
    processTable[index].blocked = 0;
    processTable[index].requestedResource = -1;

    for (int j = 0; j < NUM_RESOURCES; j++) {
        processTable[index].resourcesAllocated[j] = 0;
    }
}

void setNextLaunchTime(double interval) {
    unsigned int ns = static_cast<unsigned int>(interval * BILLION);
    nextLaunchSec = simClock->seconds;
    nextLaunchNano = simClock->nanoseconds;
    addToTime(nextLaunchSec, nextLaunchNano, ns);
}

bool timeToLaunch() {
    return timeReached(simClock->seconds, simClock->nanoseconds,
                       nextLaunchSec, nextLaunchNano);
}

void printBlockedList() {
    ostringstream out;
    out << "OSS: Blocked processes [ ";

    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].blocked) {
            out << "P" << i << "(R" << processTable[i].requestedResource << ") ";
        }
    }

    out << "]\n";
    writeLog(out.str());
}

void printResourceTable() {
    ostringstream out;
    out << "OSS: Available resources: ";
    for (int i = 0; i < NUM_RESOURCES; i++) {
        out << "R" << i << ":" << availableResources[i] << " ";
    }
    out << "\n";
    writeLog(out.str());

    writeLog("OSS: Allocated resources per process:\n");
    for (int i = 0; i < PCB_SIZE; i++) {
        if (!processTable[i].occupied) {
            continue;
        }

        ostringstream row;
        row << "P" << i << ": ";
        for (int j = 0; j < NUM_RESOURCES; j++) {
            row << processTable[i].resourcesAllocated[j] << " ";
        }
        row << "\n";
        writeLog(row.str());
    }
}

void printProcessTable() {
    ostringstream out;
    out << "\nOSS: Process Table at time "
        << simClock->seconds << ":" << simClock->nanoseconds << "\n";
    out << left
        << setw(6)  << "Entry"
        << setw(10) << "Occupied"
        << setw(10) << "PID"
        << setw(10) << "Local"
        << setw(12) << "StartSec"
        << setw(12) << "StartNano"
        << setw(10) << "Blocked"
        << setw(10) << "ReqRes"
        << "\n";
    writeLog(out.str());

    for (int i = 0; i < PCB_SIZE; i++) {
        if (!processTable[i].occupied) {
            continue;
        }

        ostringstream row;
        row << left
            << setw(6)  << i
            << setw(10) << processTable[i].occupied
            << setw(10) << processTable[i].pid
            << setw(10) << processTable[i].localPid
            << setw(12) << processTable[i].startSeconds
            << setw(12) << processTable[i].startNano
            << setw(10) << processTable[i].blocked
            << setw(10) << processTable[i].requestedResource
            << "\n";
        writeLog(row.str());
    }

    writeLog("\n");
}

void launchWorker(int index) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return;
    }

    if (pid == 0) {
        string indexStr = to_string(index);
        execl("./worker", "worker", indexStr.c_str(), (char*)nullptr);
        perror("execl");
        exit(1);
    }

    processTable[index].occupied = 1;
    processTable[index].pid = pid;
    processTable[index].localPid = index;
    processTable[index].startSeconds = simClock->seconds;
    processTable[index].startNano = simClock->nanoseconds;
    processTable[index].serviceTimeSeconds = 0;
    processTable[index].serviceTimeNano = 0;
    processTable[index].blocked = 0;
    processTable[index].requestedResource = -1;

    for (int j = 0; j < NUM_RESOURCES; j++) {
        processTable[index].resourcesAllocated[j] = 0;
    }

    readyQueue.push(index);

    writeLog("OSS: Generating process with PID " + to_string(pid) +
             " and putting it in ready queue at time " +
             to_string(simClock->seconds) + ":" +
             to_string(simClock->nanoseconds) + "\n");
}

void releaseAllResources(int index) {
    ostringstream out;
    out << "OSS: Releasing all resources from P" << index << ": ";

    bool releasedAny = false;
    for (int i = 0; i < NUM_RESOURCES; i++) {
        if (processTable[index].resourcesAllocated[i] > 0) {
            availableResources[i] += processTable[index].resourcesAllocated[i];
            out << "R" << i << ":" << processTable[index].resourcesAllocated[i] << " ";
            processTable[index].resourcesAllocated[i] = 0;
            releasedAny = true;
        }
    }

    processTable[index].requestedResource = -1;

    if (releasedAny) {
        out << "\n";
        writeLog(out.str());
    }
}

void checkBlockedProcesses() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].blocked) {
            int requested = processTable[i].requestedResource;

            if (requested >= 0 && requested < NUM_RESOURCES &&
                availableResources[requested] > 0) {

                availableResources[requested]--;
                processTable[i].resourcesAllocated[requested]++;
                processTable[i].blocked = 0;
                processTable[i].requestedResource = -1;
                readyQueue.push(i);

                writeLog("OSS: Unblocking P" + to_string(i) +
                         " and granting R" + to_string(requested) +
                         " at time " +
                         to_string(simClock->seconds) + ":" +
                         to_string(simClock->nanoseconds) + "\n");
            }
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(5);

    parseArguments(argc, argv);

    logFile.open(logFileName.c_str(), ios::out | ios::trunc);
    if (!logFile) {
        cerr << "Failed to open log file.\n";
        return 1;
    }

    key_t shmKey = ftok(".", 65);
    if (shmKey == -1) {
        perror("ftok shm");
        return 1;
    }

    shmId = shmget(shmKey, sizeof(SimClock), IPC_CREAT | 0666);
    if (shmId == -1) {
        perror("shmget");
        return 1;
    }

    simClock = (SimClock*)shmat(shmId, nullptr, 0);
    if (simClock == (void*)-1) {
        perror("shmat");
        simClock = nullptr;
        cleanup();
        return 1;
    }

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    key_t msgKey = ftok(".", 75);
    if (msgKey == -1) {
        perror("ftok msg");
        cleanup();
        return 1;
    }

    msgId = msgget(msgKey, IPC_CREAT | 0666);
    if (msgId == -1) {
        perror("msgget");
        cleanup();
        return 1;
    }

    initProcessTable();

    for (int i = 0; i < NUM_RESOURCES; i++) {
        availableResources[i] = MAX_INSTANCES;
    }

    setNextLaunchTime(0.0);

    while (finishedTotal < totalChildren || runningNow > 0) {
    while (runningNow < maxSimultaneous &&
           launchedTotal < totalChildren &&
           timeToLaunch()) {

        int index = getFreePCB();
        if (index == -1) {
            break;
        }

        launchWorker(index);
        launchedTotal++;
        runningNow++;

        setNextLaunchTime(launchInterval);
        advanceClock(DISPATCH_OVERHEAD);
    }

    checkBlockedProcesses();

    if (!readyQueue.empty()) {
        int index = readyQueue.front();
        readyQueue.pop();
        pid_t childPid = processTable[index].pid;

        writeLog("OSS: Dispatching process P" + to_string(index) +
                 " PID " + to_string(childPid) +
                 " at time " +
                 to_string(simClock->seconds) + ":" +
                 to_string(simClock->nanoseconds) + "\n");

        Message dispatchMsg;
        dispatchMsg.mtype = childPid;
        dispatchMsg.index = index;
        dispatchMsg.resource = 1;

        if (msgsnd(msgId, &dispatchMsg, sizeof(Message) - sizeof(long), 0) == -1) {
            perror("msgsnd");
            killChildren();
            cleanup();
            return 1;
        }

        Message replyMsg;
        if (msgrcv(msgId, &replyMsg, sizeof(Message) - sizeof(long), 1, 0) == -1) {
            perror("msgrcv");
            killChildren();
            cleanup();
            return 1;
        }

        int action = replyMsg.resource;

        if (action == 0) {
            writeLog("OSS: Process P" + to_string(index) +
                     " is terminating at time " +
                     to_string(simClock->seconds) + ":" +
                     to_string(simClock->nanoseconds) + "\n");

            releaseAllResources(index);
            waitpid(childPid, nullptr, 0);
            removeFromPCB(index);
            runningNow--;
            finishedTotal++;
        }
        else if (action > 0) {
            int requested = action - 1;
            totalRequests++;

            writeLog("OSS: Process P" + to_string(index) +
                     " requested R" + to_string(requested) +
                     " at time " +
                     to_string(simClock->seconds) + ":" +
                     to_string(simClock->nanoseconds) + "\n");

            if (requested >= 0 && requested < NUM_RESOURCES &&
                availableResources[requested] > 0 &&
                processTable[index].resourcesAllocated[requested] < MAX_INSTANCES) {

                availableResources[requested]--;
                processTable[index].resourcesAllocated[requested]++;
                processTable[index].requestedResource = -1;
                grantedImmediately++;

                writeLog("OSS: Granting P" + to_string(index) +
                         " request for R" + to_string(requested) + "\n");

                readyQueue.push(index);
            } else {
                processTable[index].blocked = 1;
                processTable[index].requestedResource = requested;

                writeLog("OSS: Blocking P" + to_string(index) +
                         " waiting for R" + to_string(requested) + "\n");
            }
        }
        else {
            int released = (-action) - 1;

            if (released >= 0 && released < NUM_RESOURCES &&
                processTable[index].resourcesAllocated[released] > 0) {

                processTable[index].resourcesAllocated[released]--;
                availableResources[released]++;
                totalReleases++;

                writeLog("OSS: Process P" + to_string(index) +
                         " released R" + to_string(released) +
                         " at time " +
                         to_string(simClock->seconds) + ":" +
                         to_string(simClock->nanoseconds) + "\n");
            }

            readyQueue.push(index);
        }

        advanceClock(CLOCK_INCREMENT);
    } else {
        advanceClock(IDLE_INCREMENT);
    }

    if (timeReached(simClock->seconds, simClock->nanoseconds,
                    nextDeadlockCheckSec, nextDeadlockCheckNano)) {
        runDeadlockDetection();
        addToTime(nextDeadlockCheckSec, nextDeadlockCheckNano, BILLION);
    }

    if (timeReached(simClock->seconds, simClock->nanoseconds,
                    nextTablePrintSec, nextTablePrintNano)) {
        printProcessTable();
        printBlockedList();
        printResourceTable();
        addToTime(nextTablePrintSec, nextTablePrintNano, 500000000);
    }
}

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }

    ostringstream report;
    report << "\nOSS: Final report\n";
    report << "OSS: Total processes launched: " << launchedTotal << "\n";
    report << "OSS: Total processes finished: " << finishedTotal << "\n";
    report << "OSS: Total requests: " << totalRequests << "\n";
    report << "OSS: Total releases: " << totalReleases << "\n";

    double percentGranted = 0.0;
    if (totalRequests > 0) {
        percentGranted =
            (static_cast<double>(grantedImmediately) / static_cast<double>(totalRequests)) * 100.0;
    }

    report << fixed << setprecision(2)
           << "OSS: Percent granted immediately: " << percentGranted << "%\n";
    report << "OSS: Simulation finished at time "
           << simClock->seconds << ":" << simClock->nanoseconds << "\n";

    writeLog(report.str());

    cleanup();
    logFile.close();
    return 0;
}