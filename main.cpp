#include <cstdio>
#include "mpi.h"
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <random>
#include <chrono>
#include <map>
#include <thread>
#include <iomanip>

#define ACK 1
#define REQ 2
#define RELEASE 3

struct ProcessData {
    ProcessData() = default;

    ProcessData(long long int timestamp, int rooms, bool received_ack) {
        this->timestamp = timestamp;
        this->rooms_to_sieze = rooms;
        this->received_ack = received_ack;
    }

    long long timestamp;
    int rooms_to_sieze;
    bool received_ack;
};

int RANK;
int SIZE;
int messages_received = 0;
std::map<int, ProcessData> processes_map;

long long now();

int intRand(int max);

void init(MPI_Request *table);

void sleepMillis(int millis);

void checkREQResponses(MPI_Request *requests, ProcessData *responses, bool *notFinished);

void checkACKResponses(MPI_Request *reqs, ProcessData *responses, bool *notFinished);

void initRespones(ProcessData *responses);

void initProcessMap();

using namespace std;

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &RANK);
    MPI_Comm_size(MPI_COMM_WORLD, &SIZE);
    srand((unsigned) time(nullptr) + RANK);

    MPI_Request REQS[SIZE];
    MPI_Request ACKS[SIZE];
    MPI_Request RELEASES[SIZE];
    ProcessData responses[SIZE];

    init(REQS);
    init(ACKS);
    init(RELEASES);
    initProcessMap();
    initRespones(responses);

    // send initial data
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != RANK) {
            ProcessData data = ProcessData(now(), intRand(10), false);
            printf("[#%d] Sending %d to #%d\n", RANK, data.rooms_to_sieze, destination);
            MPI_Send(&data, sizeof(struct ProcessData), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
        }
    }
    // start handles for responses
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != RANK) {
            auto process_data = processes_map.find(process_id);
            MPI_Irecv(&process_data,
                      sizeof(struct ProcessData),
                      MPI_BYTE, MPI_ANY_SOURCE, REQ, MPI_COMM_WORLD,
                      &REQS[process_id]);
            MPI_Irecv(&responses[process_id],
                      sizeof(struct ProcessData),
                      MPI_BYTE, MPI_ANY_SOURCE, REQ, MPI_COMM_WORLD,
                      &ACKS[process_id]);
            MPI_Irecv(&responses[process_id],
                      sizeof(struct ProcessData),
                      MPI_BYTE, MPI_ANY_SOURCE, REQ, MPI_COMM_WORLD,
                      &RELEASES[process_id]);
        }
    }

    // main loop
    auto notFinished = true;
    while (notFinished) {
        checkREQResponses(REQS, responses, &notFinished);
        checkACKResponses(ACKS, responses, &notFinished);
        if (notFinished) {
            sleepMillis(1000);
        }
    }

    printf("[#%d] I finished!\n", RANK);
    for (auto const&[key, value] : processes_map) {
        cout << "[" << RANK << "] Process #" << key << " timestamp=[" << value.timestamp << "],rooms_to_sieze=["
             << value.rooms_to_sieze << "]" << endl;
    }
    MPI_Finalize();
}

void checkREQResponses(MPI_Request *requests, ProcessData *responses, bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        MPI_Status status;
        if (process_id != RANK) {
            MPI_Test(&requests[process_id], &requestFinished, &status);
            if (requestFinished) {
                printf("[#%d] Received %d from #%d\n", RANK, responses[process_id].rooms_to_sieze, process_id);
                processes_map.insert({process_id, responses[process_id]});
                messages_received++;
            }
        }
    }
    *notFinished = messages_received != SIZE - 1;
}

void checkACKResponses(MPI_Request *reqs, ProcessData *responses, bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        MPI_Status status;
        if (process_id != RANK) {
            MPI_Test(&reqs[process_id], &requestFinished, &status);
            if (requestFinished) {
                printf("[#%d] Received %d from #%d\n", RANK, responses[process_id].rooms_to_sieze, process_id);
                processes_map.insert({
                                             status.MPI_SOURCE,
                                             {responses[process_id]}
                                     });
                messages_received++;
            }
        }
    }
    *notFinished = messages_received != SIZE - 1;
}

void listenForResponse(int process_id) {
    MPI_Irecv(&responses[process_id],
              sizeof(struct ProcessData),
              MPI_BYTE, MPI_ANY_SOURCE, REQ, MPI_COMM_WORLD,
              &REQS[process_id]);
}

void initProcessMap() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        processes_map.insert(process_id, {});
    }
}

void init(MPI_Request *table) {
    for (int i = 0; i < SIZE; i++) {
        table[i] = MPI_REQUEST_NULL;
    }
}

void initRespones(ProcessData *responses) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        responses[process_id] = {};
    }
}

int intRand(int max) {
    return rand() % max;
}

void sleepMillis(int millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

long long now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();;
}

