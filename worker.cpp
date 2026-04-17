#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>

using namespace std;

const int NUM_RESOURCES = 10;
const int MAX_INSTANCES = 5;

struct Message {
    long mtype;
    int index;
    int resource;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "worker: missing arguments\n";
        return 1;
    }

    int localIndex = atoi(argv[1]);

    int resourcesOwned[NUM_RESOURCES];
    for (int i = 0; i < NUM_RESOURCES; i++) {
        resourcesOwned[i] = 0;
    }

    srand(static_cast<unsigned int>(getpid() ^ time(nullptr)));

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

        Message reply;
        reply.mtype = 1;
        reply.index = localIndex;
        reply.resource = 0;

        int action = rand() % 100;

        if (action < 70) {
            int resourceIndex = rand() % NUM_RESOURCES;
            reply.resource = resourceIndex + 1;
        }
        else if (action < 90) {
            bool found = false;

            for (int i = 0; i < NUM_RESOURCES; i++) {
                if (resourcesOwned[i] > 0) {
                    reply.resource = -(i + 1);
                    resourcesOwned[i]--;
                    found = true;
                    break;
                }
            }

            if (!found) {
                int resourceIndex = rand() % NUM_RESOURCES;
                reply.resource = resourceIndex + 1;
            }
        }
        else {
            reply.resource = 0;

            if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
                perror("worker msgsnd terminate");
                return 1;
            }

            break;
        }

        if (msgsnd(msgId, &reply, sizeof(Message) - sizeof(long), 0) == -1) {
            perror("worker msgsnd");
            return 1;
        }
    }

    return 0;
}