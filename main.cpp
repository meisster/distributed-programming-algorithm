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
#define TIMEOUT 6

struct Response {
    Response() = default;

    Response(long long int timestamp, int data) : timestamp(timestamp), data(data) {}

    long long timestamp;
    int data;
};

struct ProcessData {
    ProcessData() = default;

    ProcessData(int rooms, bool receivedAck, long long int timestamp) :
            rooms(rooms), received_ack(receivedAck), timestamp(timestamp) {}

    int rooms;
    bool received_ack;
    long long timestamp;

    bool hasOlderTimestamp(Response *my_demand, int process_id, int my_id) const {
        if (this->timestamp < my_demand->timestamp) {
            return true;
        } else if (this->timestamp == my_demand->timestamp) {
            printf("[#%d] Timestamps equal! Prioritizing lower ID [%d] > [%d]\n", my_id, process_id, my_id);
            return process_id > my_id;
        } else {
            return false;
        };
    }

    bool receivedACK() {
        return this->received_ack;
    }
};

struct ProcessRequestResponse {
    ProcessRequestResponse() = default;

    ProcessRequestResponse(bool requestCompleted, MPI_Request request, const Response &response) :
            requestCompleted(requestCompleted), request(request), response(response) {}

    bool requestCompleted;
    MPI_Request request;
    Response response;
};

static int MYSELF;
static int SIZE;
static int MESSAGES_RECEIVED = 0;
static int ROOMS_WANTED = 0;
static int ROOMS_OCCUPIED = 0;
static int ACKS_ACQUIRED = 0;
static std::map<int, ProcessData> PROCESSES_MAP;
static Response MY_DEMAND;
std::vector<ProcessRequestResponse> REQS;
std::vector<ProcessRequestResponse> ACKS;
std::vector<ProcessRequestResponse> RELEASES;
std::vector<ProcessData> RESPONSES;

long long now();

int atLeastOne();

void init(std::vector<ProcessRequestResponse> *table);

void sleepMillis(int millis);

void checkREQ(bool *notFinished);

void checkACK(bool *notFinished);

void checkRELEASE(bool *notFinished);

void sendACK(int destination);

void initRespones();

void initProcessMap();

void listenFor(int TAG, ProcessRequestResponse *process, int process_id);

void sendREQ();

void tryToOccupyRooms(bool *finished);

void sendRELEASE();

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
    sendREQ();
//
    // start handles for responses
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            listenFor(REQ, &REQS[process_id], process_id);
            listenFor(RELEASE, &RELEASES[process_id], process_id);
        }
    }

    // main loop
    auto finished = false;
    auto timeout = false;
    auto start = chrono::steady_clock::now();

    while (!finished && !timeout) {
        checkREQ(&finished);
        checkACK(&finished);
        checkRELEASE(&finished);
        if (MESSAGES_RECEIVED >= (SIZE - 1)) {
            tryToOccupyRooms(&finished);
        }

        auto end = chrono::steady_clock::now();
        timeout = chrono::duration_cast<chrono::seconds>(end - start).count() > TIMEOUT;
        if (!finished & !timeout) {
            sleepMillis(100);
        }
        if (timeout) {
            printf("[#%d][INFO]Timeout!\n", MYSELF);
        }
    }

    for (auto const&[key, value] : PROCESSES_MAP) {
        if (key != MYSELF) {
            cout << "[#" << MYSELF << "][INFO] Process #" << key << " rooms=[" << value.rooms << "],ack=["
                 << value.received_ack << "]" << endl;
        }
    }
    MPI_Finalize();
}

void tryToOccupyRooms(bool *finished) {
    bool iCanGoIn = false;
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            auto condition = PROCESSES_MAP.find(process_id)->second.hasOlderTimestamp(&MY_DEMAND, process_id, MYSELF) ||
                             PROCESSES_MAP.find(process_id)->second.receivedACK();
            printf("[#%d][OCCUPY] Process #%d hasOlderTimestamp=%d, receivedACK=%d\n", MYSELF, process_id, PROCESSES_MAP.find(process_id)->second.hasOlderTimestamp(&MY_DEMAND, process_id, MYSELF), PROCESSES_MAP.find(process_id)->second.receivedACK());
            iCanGoIn = condition;
            if (!condition) {
                iCanGoIn = false;
                break;
            }
        }
    }
    printf("[#%d][OCCUPY] ROOMS_WANTED=[%d], ACKS_ACQUIRED=[%d]\n", MYSELF, ROOMS_WANTED, ACKS_ACQUIRED);
    if (ROOMS_WANTED - ACKS_ACQUIRED <= MAX_ROOMS && iCanGoIn) {
        printf("[#%d][OCCUPY] I have taken the rooms=[%d]! Going to sleep for 2 seconds\n", MYSELF, MY_DEMAND.data);
        *finished = true;
        sleepMillis(2000);
        sendRELEASE();
    } else {
        printf("[#%d][OCCUPY] Cannot occupy room, waiting...\n", MYSELF);
        sleepMillis(1000);
    }
}

void checkREQ(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        if (process_id != MYSELF) {
            MPI_Test(&REQS[process_id].request, &requestFinished, MPI_STATUS_IGNORE);
            if (requestFinished && !REQS[process_id].requestCompleted) {
                MESSAGES_RECEIVED++;
                printf("[#%d][%s][RECEIVE] Received %d_%lld from #%d\n", MYSELF, "REQ", REQS[process_id].response.data,
                       REQS[process_id].response.timestamp,
                       process_id);
                REQS[process_id].requestCompleted = true;
                PROCESSES_MAP.find(process_id)->second.rooms = REQS[process_id].response.data;
                PROCESSES_MAP.find(process_id)->second.timestamp = REQS[process_id].response.timestamp;
                ROOMS_WANTED += REQS[process_id].response.data;
                if (REQS[process_id].response.timestamp < MY_DEMAND.timestamp) {
                    sendACK(process_id);
                }
            }
        }
    }
}

void checkACK(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        if (process_id != MYSELF) {
            MPI_Test(&ACKS[process_id].request, &requestFinished, MPI_STATUS_IGNORE);
            if (requestFinished && !ACKS[process_id].requestCompleted) {
                printf("[#%d][%s][RECEIVE] Received %d_%lld from #%d\n", MYSELF, "ACK", ACKS[process_id].response.data,
                       ACKS[process_id].response.timestamp,
                       process_id);
                ACKS[process_id].requestCompleted = true;
                PROCESSES_MAP.find(process_id)->second.received_ack = ACKS[process_id].response.data;
                ACKS_ACQUIRED += PROCESSES_MAP.find(process_id)->second.rooms;
            }
        }
    }
}

void checkRELEASE(bool *notFinished) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        int requestFinished = 0;
        if (process_id != MYSELF) {
            printf("[#%d][%s][CHECK] Checking for RELEASE from #%d current=%d\n", MYSELF, "RELEASE", process_id, RELEASES[process_id].response.data);
            MPI_Test(&RELEASES[process_id].request, &requestFinished, MPI_STATUS_IGNORE);
            if (requestFinished) {
                printf("[#%d][%s][RECEIVE] Received %d_%lld from #%d\n", MYSELF, "RELEASE", RELEASES[process_id].response.data,
                       RELEASES[process_id].response.timestamp,
                       process_id);
                RELEASES[process_id].requestCompleted = true;
                PROCESSES_MAP.find(process_id)->second.rooms -= RELEASES[process_id].response.data;
                ROOMS_WANTED -= RELEASES[process_id].response.data;
            }
        }
    }
}

void sendACK(int destination) {
    auto response = Response(now(), true);
    printf("[#%d][ACK][SEND] Sending ACK to #%d\n", MYSELF, destination);
    MPI_Send(&response, sizeof(struct Response), MPI_BYTE, destination, ACK, MPI_COMM_WORLD);
    listenFor(RELEASE, &RELEASES[destination], destination);
}

void sendRELEASE() {
    auto demand = Response(now(), MY_DEMAND.data);
    ROOMS_WANTED -= demand.data;
    MY_DEMAND = {}; // will it work?
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            printf("[#%d][%s][SEND] Sending RELEASE rooms=[%d] to #%d\n", MYSELF, "RELEASE", demand.data, destination);
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, RELEASE, MPI_COMM_WORLD);
//            listenFor(ACK, &ACKS[destination], destination); // will it work?
        }
    }
}

void sendREQ() {
    auto demand = Response(now(), atLeastOne());
    MY_DEMAND = demand;
    ROOMS_WANTED += demand.data;
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            printf("[#%d][%s][SEND] Sending %d_%lld to #%d\n", MYSELF, "REQ", demand.data, demand.timestamp, destination);
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
            ACKS[destination].requestCompleted = false;
            listenFor(ACK, &ACKS[destination], destination);
        }
    }
}

void listenFor(int TAG, ProcessRequestResponse *process, int process_id) {
    MPI_Irecv(&process->response,
              sizeof(struct Response),
              MPI_BYTE, process_id, TAG, MPI_COMM_WORLD,
              &process->request);
}

void initProcessMap() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        auto d = ProcessData();
        PROCESSES_MAP.insert({process_id, d});
    }
}

void init(vector<ProcessRequestResponse> *table) {
    table->resize(SIZE);
    for (int process_id = 0; process_id < SIZE; process_id++) {
        table->at(process_id).request = MPI_REQUEST_NULL;
        table->at(process_id).response = {};
        table->at(process_id).requestCompleted = false;
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
    this_thread::sleep_for(chrono::milliseconds(millis));
}

long long now() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

