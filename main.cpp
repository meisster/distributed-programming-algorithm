#include <cstdio>
#include "mpi.h"
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <random>
#include <chrono>
#include <map>
#include <thread>

#define ACK 1
#define REQ 2
#define RELEASE 3
#define MAX_ROOMS 10

struct ProcessData {
    ProcessData() = default;

    ProcessData(int rooms, bool receivedAck) : rooms(rooms), received_ack(receivedAck) {}

    int rooms;
    bool received_ack;
};

struct Response {
    Response() = default;

    Response(long long int timestamp, int data) : timestamp(timestamp), data(data) {}

    long long timestamp;
    int data;
};

struct ProcessRequest {
    ProcessRequest() = default;

    ProcessRequest(MPI_Request request, const Response &response) : request(request), response(response) {}

    MPI_Request request;
    Response response;
};

static int MYSELF;
static int SIZE;
static int MESSAGES_RECEIVED = 0;
static std::map<int, ProcessData> PROCESSES_MAP;
static Response MY_DEMAND;
std::vector<ProcessRequest> REQS;
std::vector<ProcessRequest> ACKS;
std::vector<ProcessRequest> RELEASES;
std::vector<ProcessData> RESPONSES;

long long now();

int atLeastOne();

void init(std::vector<ProcessRequest> *table);

void sleepMillis(int millis);

void checkREQ(bool *notFinished);

void checkACK(bool *notFinished);

void checkRELEASE(bool *notFinished);

void initRespones();

void initProcessMap();

void listenFor(int TAG, ProcessRequest *process);

void demandRooms();

using namespace std;

int main(int argc, char **argv) {
    srand((unsigned) time(nullptr) + MYSELF);
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &MYSELF);
    MPI_Comm_size(MPI_COMM_WORLD, &SIZE);
    init(&REQS);
    init(&ACKS);
    init(&RELEASES);
    initProcessMap();
    initRespones();

    // send initial data
    demandRooms();
//
    // start handles for responses
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            listenFor(REQ, &REQS[process_id]);
            listenFor(ACK, &ACKS[process_id]);
            listenFor(RELEASE, &RELEASES[process_id]);
        }
    }

    // main loop
    auto notFinished = true;
    while (notFinished) {
        checkREQ(&notFinished);
        checkACK(&notFinished);
        checkRELEASE(&notFinished);

        if (notFinished) {
            sleepMillis(100);
        }
    }

    printf("[#%d] I finished!\n", MYSELF);
    for (auto const&[key, value] : PROCESSES_MAP) {
        cout << "[" << MYSELF << "] Process #" << key << " rooms=[" << value.rooms << "],ack=["
             << value.received_ack << "]" << endl;
    }
    MPI_Finalize();
}

void checkREQ(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        MPI_Status status;
        if (process_id != MYSELF) {
            MPI_Test(&REQS[process_id].request, &requestFinished, &status);
            if (requestFinished) {
                printf("[#%d][%s] Received %d from #%d\n", MYSELF, "REQ", REQS[process_id].response.data,
                       process_id);
                auto process = PROCESSES_MAP.find(process_id);
                process->second.rooms = REQS[process_id].response.data;
                MESSAGES_RECEIVED++;
            }
        }
    }
    *notFinished = MESSAGES_RECEIVED != SIZE - 1;
}

void checkACK(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        MPI_Status status;
        if (process_id != MYSELF) {
            MPI_Test(&ACKS[process_id].request, &requestFinished, &status);
            if (requestFinished) {
                printf("[#%d][%s] Received %d from #%d\n", MYSELF, "ACK", ACKS[process_id].response.data,
                       process_id);
                auto process = PROCESSES_MAP.find(process_id);
                process->second.received_ack = ACKS[process_id].response.data;
                MESSAGES_RECEIVED++;
            }
        }
    }
}

void checkRELEASE(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        MPI_Status status;
        if (process_id != MYSELF) {
            MPI_Test(&RELEASES[process_id].request, &requestFinished, &status);
            if (requestFinished) {
                printf("[#%d][%s] Received %d from #%d\n", MYSELF, "RELEASE", RELEASES[process_id].response.data,
                       process_id);
                auto process = PROCESSES_MAP.find(process_id);
                process->second.rooms -= RELEASES[process_id].response.data;
                MESSAGES_RECEIVED++;
            }
        }
    }
}

void demandRooms() {
    Response demand = Response(now(), atLeastOne());
    MY_DEMAND = demand;
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            printf("[#%d][%s] Sending %d to #%d\n", MYSELF, "REQ", demand.data, destination);
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
        }
    }
}

void listenFor(int TAG, ProcessRequest *process) {
    MPI_Irecv(&process->response,
              sizeof(struct Response),
              MPI_BYTE, MPI_ANY_SOURCE, TAG, MPI_COMM_WORLD,
              &process->request);
}

void initProcessMap() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        auto d = ProcessData();
        PROCESSES_MAP.insert({process_id, d});
    }
}

void init(std::vector<ProcessRequest> *table) {
    table->resize(SIZE);
    for (int process_id = 0; process_id < SIZE; process_id++) {
        table->at(process_id).request = MPI_REQUEST_NULL;
        table->at(process_id).response = {};
    }
}

void initRespones() {
    RESPONSES.resize(SIZE);
    for (int process_id = 0; process_id < SIZE; process_id++) {
        RESPONSES[process_id] = {};
    }
}

int atLeastOne() {
    return (rand() % MAX_ROOMS) + 1;
}

void sleepMillis(int millis) {
    std::this_thread::sleep_for(std::chrono::milliseconds(millis));
}

long long now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();;
}

