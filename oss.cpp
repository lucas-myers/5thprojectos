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



int availableResources[NUM_RESOURCES];

using namespace std;

const int MAX_TOTAL_PROCESSES = 20;
const int PCB_SIZE = 18;
const unsigned int BILLION = 1000000000;
const unsigned int BASE_QUANTUM = 25000000;   // 25ms
const unsigned int BLOCK_TIME = 100000000;    // 100ms
const unsigned int IDLE_INCREMENT = 100000;   // 100 microseconds
const unsigned int DISPATCH_OVERHEAD = 1000;  // 1000 ns
const unsigned int UNBLOCK_OVERHEAD = 5000;   // 5000 ns
const int MAX_LOG_LINES = 10000;
const int NUM_RESOURCES = 10; //resource
const int MAX_INSTANCES = 5;

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

    // PROJECT 5
    int resourcesAllocated[NUM_RESOURCES];
    int requestedResource;
};

struct Message {
    long mtype;
    int index;
    int quantum;
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

unsigned long long cpuBusyTime = 0;
unsigned long long systemOverheadTime = 0;
unsigned long long idleTime = 0;

void writeLog(const string& text) {
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

void addServiceTime(int index, unsigned int ns) {
    addToTime(processTable[index].serviceTimeSeconds,
              processTable[index].serviceTimeNano,
              ns);
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

void printReadyQueue() {
    queue<int> temp = readyQueue;
    string line = "OSS: Ready queue [ ";

    while (!temp.empty()) {
        int index = temp.front();
        temp.pop();
        line += "P" + to_string(index) + " ";
    }

    line += "]\n";
    writeLog(line);
}

void printBlockedList() {
    ostringstream out;
    out << "OSS: Blocked queue [ ";

    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].blocked) {
            out << "P" << i << " ";
        }
    }

    out << "]\n";
    writeLog(out.str());
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
        << setw(12) << "ServSec"
        << setw(12) << "ServNano"
        << setw(10) << "Blocked"
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
            << setw(12) << processTable[i].serviceTimeSeconds
            << setw(12) << processTable[i].serviceTimeNano
            << setw(10) << processTable[i].blocked
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
        string burstStr = to_string(timeLimitForChildren);
        string indexStr = to_string(index);

        execl("./worker", "worker", burstStr.c_str(), indexStr.c_str(), (char*)nullptr);
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

void checkBlockedProcesses() {
    for (int i = 0; i < PCB_SIZE; i++) {
        if (processTable[i].occupied && processTable[i].blocked) {
            if (timeReached(simClock->seconds, simClock->nanoseconds,
                            processTable[i].eventWaitSec,
                            processTable[i].eventWaitNano)) {

                processTable[i].blocked = 0;
                readyQueue.push(i);

                writeLog("OSS: Unblocking process with PID " +
                         to_string(processTable[i].pid) +
                         " and putting it in ready queue at time " +
                         to_string(simClock->seconds) + ":" +
                         to_string(simClock->nanoseconds) + "\n");

                advanceClock(UNBLOCK_OVERHEAD);
                systemOverheadTime += UNBLOCK_OVERHEAD;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGALRM, signalHandler);
    alarm(3);

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
            systemOverheadTime += DISPATCH_OVERHEAD;
        }

        checkBlockedProcesses();

        if (!readyQueue.empty()) {
            printReadyQueue();

            int index = readyQueue.front();
            readyQueue.pop();
            pid_t childPid = processTable[index].pid;

            writeLog("OSS: Dispatching process with PID " +
                     to_string(childPid) +
                     " from ready queue at time " +
                     to_string(simClock->seconds) + ":" +
                     to_string(simClock->nanoseconds) + "\n");

            advanceClock(DISPATCH_OVERHEAD);
            systemOverheadTime += DISPATCH_OVERHEAD;
            writeLog("OSS: total time this dispatch was 1000 nanoseconds\n");

            Message dispatchMsg;
            dispatchMsg.mtype = childPid;
            dispatchMsg.index = index;
            dispatchMsg.quantum = BASE_QUANTUM;

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

            int usedTime = replyMsg.quantum;

            if (usedTime < 0) {
                usedTime = -usedTime;

                writeLog("OSS: Receiving that process with PID " +
                         to_string(childPid) +
                         " terminated after running for " +
                         to_string(usedTime) + " nanoseconds\n");

                advanceClock(static_cast<unsigned int>(usedTime));
                cpuBusyTime += static_cast<unsigned int>(usedTime);
                addServiceTime(index, static_cast<unsigned int>(usedTime));

                waitpid(childPid, nullptr, 0);

                removeFromPCB(index);
                runningNow--;
                finishedTotal++;

                writeLog("OSS: Process with PID " +
                         to_string(childPid) +
                         " removed from system\n");
            }
            else if (usedTime < static_cast<int>(BASE_QUANTUM)) {
                writeLog("OSS: Receiving that process with PID " +
                         to_string(childPid) +
                         " ran for " + to_string(usedTime) +
                         " nanoseconds\n");
                writeLog("OSS: not using its entire time quantum\n");
                writeLog("OSS: Putting process with PID " +
                         to_string(childPid) +
                         " into blocked queue\n");

                advanceClock(static_cast<unsigned int>(usedTime));
                cpuBusyTime += static_cast<unsigned int>(usedTime);
                addServiceTime(index, static_cast<unsigned int>(usedTime));

                processTable[index].blocked = 1;
                processTable[index].eventWaitSec = simClock->seconds;
                processTable[index].eventWaitNano = simClock->nanoseconds;
                addToTime(processTable[index].eventWaitSec,
                          processTable[index].eventWaitNano,
                          BLOCK_TIME);

                writeLog("OSS: Process with PID " +
                         to_string(childPid) +
                         " will unblock at time " +
                         to_string(processTable[index].eventWaitSec) + ":" +
                         to_string(processTable[index].eventWaitNano) + "\n");
            }
            else {
                writeLog("OSS: Receiving that process with PID " +
                         to_string(childPid) +
                         " ran for " + to_string(usedTime) +
                         " nanoseconds\n");

                advanceClock(static_cast<unsigned int>(usedTime));
                cpuBusyTime += static_cast<unsigned int>(usedTime);
                addServiceTime(index, static_cast<unsigned int>(usedTime));

                readyQueue.push(index);

                writeLog("OSS: Putting process with PID " +
                         to_string(childPid) +
                         " into ready queue\n");
            }
        } else {
            if (launchedTotal < totalChildren && !timeToLaunch()) {
                advanceClock(IDLE_INCREMENT);
                idleTime += IDLE_INCREMENT;
            } else if (runningNow > 0) {
                advanceClock(IDLE_INCREMENT);
                idleTime += IDLE_INCREMENT;
            }
        }

        if (timeReached(simClock->seconds, simClock->nanoseconds,
                        nextTablePrintSec, nextTablePrintNano)) {
            printProcessTable();
            printBlockedList();
            addToTime(nextTablePrintSec, nextTablePrintNano, 500000000);
        }
    }

    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }

    unsigned long long totalSimulatedTime =
        static_cast<unsigned long long>(simClock->seconds) * BILLION +
        simClock->nanoseconds;

    double cpuUtilization = 0.0;
    if (totalSimulatedTime > 0) {
        cpuUtilization =
            (static_cast<double>(cpuBusyTime) / static_cast<double>(totalSimulatedTime)) * 100.0;
    }

    writeLog("\nOSS: Total processes launched: " + to_string(launchedTotal) + "\n");
    writeLog("OSS: Total processes finished: " + to_string(finishedTotal) + "\n");
    writeLog("OSS: Total CPU busy time: " + to_string(cpuBusyTime) + " ns\n");
    writeLog("OSS: Total system overhead time: " + to_string(systemOverheadTime) + " ns\n");
    writeLog("OSS: Total idle time: " + to_string(idleTime) + " ns\n");
    writeLog("OSS: Simulation finished at time " +
             to_string(simClock->seconds) + ":" +
             to_string(simClock->nanoseconds) + "\n");

    ostringstream finalReport;
    finalReport << fixed << setprecision(2)
                << "OSS: Average CPU utilization: " << cpuUtilization << "%\n";
    writeLog(finalReport.str());

    cleanup();
    logFile.close();
    return 0;
}