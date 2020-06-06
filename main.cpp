#include <cstdio>
#include "mpi.h"
#include "./colors.h"
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
#define ERROR_CODE 404
#define MAX_ROOMS 10
#define TIMEOUT 5

struct Response {
    Response() = default;

    Response(long long int timestamp, int data, bool dirty) : timestamp(timestamp), data(data), dirty(dirty) {}

    long long timestamp;
    int data;
    bool dirty;
};

struct ProcessData {
    ProcessData() = default;

    ProcessData(int rooms, bool receivedAck, long long int timestamp) :
            rooms(rooms), received_ack(receivedAck), timestamp(timestamp), sent_ACK(false) {}

    bool sent_ACK;
    int rooms;
    bool received_ack;
    long long timestamp;

    bool hasOlderTimestamp(Response *my_demand, int process_id, int my_id) const {
        if (this->timestamp < my_demand->timestamp && this->timestamp != 0) {
            return true;
        } else if (this->timestamp == my_demand->timestamp) {
            printf("[#%d][INFO] Timestamps equal! Prioritizing lower ID [%d] > [%d]\n", my_id, process_id, my_id);
            return process_id > my_id;
        } else {
            return false;
        };
    }

    bool receivedACK() {
        return this->received_ack;
    }
};

struct ProcessResponse {
    ProcessResponse() = default;

    ProcessResponse(const Response &response) : response(response) {}

    Response response;
};

static int MYSELF;
static int SIZE;
static int ACKS_OFFSET;
static int RELEASE_OFFSET;
static int MESSAGES_RECEIVED = 0;
static int ROOMS_WANTED = 0;
static int ACKS_ACQUIRED = 0;
static std::map<int, ProcessData> PROCESSES_MAP;
static Response MY_DEMAND;
static int ERROR;
std::vector<ProcessResponse> REQS;
std::vector<ProcessResponse> ACKS;
std::vector<ProcessResponse> RELEASES;
std::vector<ProcessData> RESPONSES;

long long now();

int atLeastOne();

void init(std::vector<ProcessResponse> *table);

void sleepMillis(int millis);

void checkREQ(int *indexes, MPI_Request *requests);

void checkACK(int *indexes, MPI_Request *requests);

void checkRELEASE(int *indexes, MPI_Request *requests);

bool checkForErrors(const MPI_Request *error, int errorCode);

void sendACK(int destination);

void initRespones();

void initProcessMap();

void listenFor(int TAG, ProcessResponse *process, int process_id, MPI_Request *request);

void sendREQ();

void timeout();

void tryToOccupyRooms(int *indexes);

void sendRELEASE();

void initRequests(MPI_Request *requests);

void sendACKToRest();

using namespace std;

int main(int argc, char **argv) {
    srand((unsigned) time(nullptr) + MYSELF);
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &MYSELF);
    MPI_Comm_size(MPI_COMM_WORLD, &SIZE);
    MPI_Request requests[(SIZE * 3) + 1];
    ACKS_OFFSET = SIZE;
    RELEASE_OFFSET = 2 * SIZE;
    ERROR = SIZE * 3;

    initRequests(requests);
    init(&REQS);
    init(&ACKS);
    init(&RELEASES);
    initProcessMap();
    ProcessResponse timeout_response = {};
    listenFor(ERROR_CODE, &timeout_response, MPI_ANY_SOURCE, &requests[ERROR]);
    thread(timeout).detach();
    // send initial data
    sendREQ();
    // start handles for responses
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            listenFor(REQ, &REQS[process_id], process_id, &requests[process_id]);
            listenFor(ACK, &ACKS[process_id], process_id, &requests[ACKS_OFFSET + process_id]);
            listenFor(RELEASE, &RELEASES[process_id], process_id, &requests[RELEASE_OFFSET + process_id]);

        }
    }

    // main loop
    auto finished = false;
    int indexes[(SIZE * 3) + 1];
    int count = 0;
    while (!finished) {
        MPI_Waitsome((SIZE * 3) + 1, requests, &count, indexes, MPI_STATUSES_IGNORE);
        finished = checkForErrors(&requests[ERROR], count);
//        printf("[#%d] Got %d requests!\n", MYSELF, count);
        checkACK(indexes, requests);
        checkRELEASE(indexes, requests);
        checkREQ(indexes, requests);
        tryToOccupyRooms(indexes);
    }

//    for (auto const&[key, value] : PROCESSES_MAP) {
//        if (key != MYSELF) {
//            cout << "[#" << MYSELF << "][INFO] Process #" << key << " rooms=[" << value.rooms << "],ack=["
//                 << value.received_ack << "]" << endl;
//        }
//    }
    MPI_Finalize();
}

bool checkForErrors(const MPI_Request *error, int errorCode) {
    if (*error == MPI_REQUEST_NULL || errorCode == MPI_UNDEFINED) {
        printf("[#%d] %s\n", MYSELF, LRED("Error has occured, shutting down!"));
        return true;
    } else {
        return false;
    };
}

void tryToOccupyRooms(int *indexes) {
    bool iCanGoIn = false;
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            auto condition = PROCESSES_MAP.find(process_id)->second.hasOlderTimestamp(&MY_DEMAND, process_id, MYSELF) ||
                             PROCESSES_MAP.find(process_id)->second.receivedACK();
//            printf("[#%d][OCCUPY] Process #%d hasOlderTimestamp=%d, receivedACK=%d\n", MYSELF, process_id, PROCESSES_MAP.find(process_id)->second.hasOlderTimestamp(&MY_DEMAND, process_id, MYSELF), PROCESSES_MAP.find(process_id)->second.receivedACK());
            iCanGoIn = condition;
            if (!condition) {
                iCanGoIn = false;
                break;
            }
        }
    }
    printf("[#%d][%s] %s=[%d], %s=[%d]\n", MYSELF, YELL("OCC"),
           LYELLOW("ROOMS_WANTED"), ROOMS_WANTED, LYELLOW("ACKS_ACQUIRED"), ACKS_ACQUIRED);
    if (ROOMS_WANTED - ACKS_ACQUIRED <= MAX_ROOMS && iCanGoIn) {
        auto timestamp = now();
        printf("[#%d][%s][%s] %s=[%d]!\n", MYSELF, YELL("OCC"), LGREEN("SCC"),
               LGREEN("I have taken the rooms"), MY_DEMAND.data);
        sleepMillis(2000);
        sendRELEASE();
        sendACKToRest();
        sleepMillis(200);
        sendREQ();
    }
}

void sendACKToRest() {
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF && !process.sent_ACK) {
            sendACK(process_id);
        }
        // cleanup
        PROCESSES_MAP.at(process_id).sent_ACK = false;
    }
}

void checkREQ(int *indexes, MPI_Request *requests) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && REQS[process_id].response.dirty) {
            printf("[#%d][%s][%s] Got REQ rooms=[%d] from %d!\n", MYSELF, LRED("REQ"), LGREEN("RCV"),
                   REQS[process_id].response.data, process_id);
            listenFor(REQ, &REQS[process_id], process_id, &requests[process_id]);
            REQS[process_id].response.dirty = false;
            PROCESSES_MAP.at(process_id).rooms = REQS[process_id].response.data;
            PROCESSES_MAP.at(process_id).timestamp = REQS[process_id].response.timestamp;
            ROOMS_WANTED += REQS[process_id].response.data;
            if (REQS[process_id].response.timestamp < MY_DEMAND.timestamp) {
                sendACK(process_id);
            }
        }
    }
}

void checkACK(int *indexes, MPI_Request *requests) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && ACKS[process_id].response.dirty) {
            listenFor(ACK, &ACKS[process_id], process_id, &requests[ACKS_OFFSET + process_id]);
            ACKS[process_id].response.dirty = false;
            if (ACKS[process_id].response.timestamp == MY_DEMAND.timestamp) {
                printf("[#%d][%s][%s] Got ACK from %d!\n", MYSELF, LBLUE("ACK"), LGREEN("RCV"), process_id);
                PROCESSES_MAP.at(process_id).received_ack = ACKS[process_id].response.data;
                ACKS_ACQUIRED += PROCESSES_MAP.find(process_id)->second.rooms;
            }
        }
    }
}

void checkRELEASE(int *indexes, MPI_Request *requests) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && RELEASES[process_id].response.dirty) {
            printf("[#%d][%s][%s] RELEASE rooms=[%d] from #%d!\n", MYSELF, LMAG("RLS"), LGREEN("RCV"),
                    RELEASES[process_id].response.data, process_id);
            listenFor(RELEASE, &RELEASES[process_id], process_id, &requests[RELEASE_OFFSET + process_id]);
            RELEASES[process_id].response.dirty = false;
            PROCESSES_MAP.at(process_id).rooms -= RELEASES[process_id].response.data;
            ROOMS_WANTED -= RELEASES[process_id].response.data;
        }
    }
}

void sendACK(int destination) {
    PROCESSES_MAP.at(destination).sent_ACK = true;
    auto response = Response(REQS[destination].response.timestamp, true, true);
    printf("[#%d][%s][%s] Sending ACK to #%d\n", MYSELF, LBLUE("ACK"), LYELLOW("SND"), destination);
    MPI_Send(&response, sizeof(struct Response), MPI_BYTE, destination, ACK, MPI_COMM_WORLD);
}

void sendRELEASE() {
    auto demand = Response(now(), MY_DEMAND.data, true);
    ROOMS_WANTED -= demand.data;
    MY_DEMAND = {};
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            printf("[#%d][%s][%s] Sending RELEASE rooms=[%d] to #%d\n", MYSELF, LMAG("RLS"), LYELLOW("SND"),
                   demand.data, destination);
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, RELEASE, MPI_COMM_WORLD);
        }
    }
}

void sendREQ() {
    auto demand = Response(now(), atLeastOne(), true);
    MY_DEMAND = demand;
    ROOMS_WANTED += demand.data;
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            printf("[#%d][%s][%s] Requesting rooms=[%d][%lld] to #%d\n", MYSELF, LRED("REQ"), LYELLOW("SND"),
                   demand.data, demand.timestamp,
                   destination);
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
//            PROCESSES_MAP.at(destination).timestamp = 0;
            PROCESSES_MAP.at(destination).received_ack = false;
//            PROCESSES_MAP.at(destination).rooms = 0;
        }
    }
    ACKS_ACQUIRED = 0;
}

void listenFor(int TAG, ProcessResponse *process, int process_id, MPI_Request *request) {
    MPI_Irecv(&process->response,
              sizeof(struct Response),
              MPI_BYTE, process_id, TAG, MPI_COMM_WORLD,
              request);
}

void initProcessMap() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        auto d = ProcessData();
        PROCESSES_MAP.insert({process_id, d});
    }
}

void initRequests(MPI_Request *requests) {
    for (int i = 0; i < (SIZE * 3) + 1; i++) {
        requests[i] = MPI_REQUEST_NULL;
    }
}

void init(vector<ProcessResponse> *table) {
    table->resize(SIZE);
    for (int process_id = 0; process_id < SIZE; process_id++) {
        table->at(process_id).response = {};
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

void timeout() {
    auto timeout = false;
    auto response = Response(now(), 1, true);
    auto start = chrono::steady_clock::now();
    while (!timeout) {
        auto end = chrono::steady_clock::now();
        timeout = chrono::duration_cast<chrono::seconds>(end - start).count() > TIMEOUT;
        sleepMillis(500);
    }
    MPI_Send(&response, sizeof(struct Response), MPI_BYTE, MYSELF, ERROR_CODE, MPI_COMM_WORLD);
}