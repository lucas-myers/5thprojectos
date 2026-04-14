#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

const int NUM_RESOURCES = 10;
const int MAX_INSTANCES = 5;

using namespace std;

struct Message {
    long mtype;
    int index;
    int quantum;
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "worker: missing arguments\n";
        return 1;
    }

    double burstLimitSeconds = atof(argv[1]);
    int localIndex = atoi(argv[2]);

    int totalBurstNano = static_cast<int>(burstLimitSeconds * 1000000000.0);
    if (totalBurstNano <= 0) {
        totalBurstNano = 1000000;
    }

    int usedSoFar = 0;

    srand(static_cast<unsigned int>(getpid() ^ time(nullptr)));

    usleep(100000);

    key_t msgKey = ftok(".", 75);
    if (msgKey == -1) {
        perror("worker ftok");
        return 1;
    }

    int msgId = msgget(msgKey, 0666);
    if (msgId == -1) {
        perror("worker msgget");
        return 1;
    }

    while (true) {
        Message msg;

        if (msgrcv(msgId, &msg, sizeof(Message) - sizeof(long), getpid(), 0) == -1) {
            perror("worker msgrcv");
            return 1;
        }

        int quantum = msg.quantum;
        int remaining = totalBurstNano - usedSoFar;

        Message reply;
        reply.mtype = 1;
        reply.index = localIndex;

        if (remaining <= quantum) {
            usedSoFar += remaining;
            reply.quantum = -remaining;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd terminate");
                return 1;
            }

            break;
        }

        int blockChance = rand() % 100;

        if (blockChance < 20) {
            int used = (rand() % (quantum - 1)) + 1;

            if (used > remaining) {
                used = remaining;
            }

            usedSoFar += used;
            reply.quantum = used;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd blocked");
                return 1;
            }
        } else {
            usedSoFar += quantum;
            reply.quantum = quantum;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd full");
                return 1;
            }
        }
    }

    return 0;
}