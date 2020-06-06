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
#include <mutex>

#define ACK 1
#define REQ 2
#define RELEASE 3
#define ERROR_CODE 404
#define MAX_ROOMS 10
#define TIMEOUT 20
#define ELEVATORS 2

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
            rooms(rooms), received_ack_from(receivedAck), timestamp(timestamp) {}

    int rooms;
    bool received_ack_from;
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
        return this->received_ack_from;
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
static int ROOMS_WANTED = 0;
static int ACKS_ACQUIRED = 0;
static std::map<int, ProcessData> PROCESSES_MAP;
static Response MY_DEMAND;
static int ERROR;
std::vector<ProcessResponse> REQS;
std::vector<ProcessResponse> ACKS;
std::vector<ProcessResponse> RELEASES;

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

void sendACKToEveryone();

int countACKS();

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
    while (true) {
        MPI_Waitsome((SIZE * 3) + 1, requests, &count, indexes, MPI_STATUSES_IGNORE);
        finished = checkForErrors(&requests[ERROR], count);
        if (finished) break;
        checkACK(indexes, requests);
        checkRELEASE(indexes, requests);
        checkREQ(indexes, requests);
        tryToOccupyRooms(indexes);
    }

//    for (auto const&[key, value] : PROCESSES_MAP) {
//        if (key != MYSELF) {
//            cout << "[#" << MYSELF << "][INFO] Process #" << key << " rooms=[" << value.rooms << "],ack=["
//                 << value.received_ack_from << "]" << endl;
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
//    printf("[%s][#%d] %s=[%d], %s=[%d]\n", YELL("OCC"), MYSELF,
//           LYELLOW("ROOMS_WANTED"), ROOMS_WANTED, LYELLOW("ACKS_ACQUIRED"), ACKS_ACQUIRED);
    if (ROOMS_WANTED - ACKS_ACQUIRED <= MAX_ROOMS && iCanGoIn && countACKS() >= SIZE - ELEVATORS) {
        printf("[%s][%s][#%d] %s=[%d] with %d ACKS!\n", YELL("OCC"), LGREEN("SCC"), MYSELF,
               LGREEN("I have taken the rooms"), MY_DEMAND.data, countACKS());
        sleepMillis(5000); // enjoy your time in isolation!
        sendRELEASE();
        sleepMillis(500); // think about sending new request
        sendREQ();
        sendACKToEveryone(); // put myself at the end the line
    }
}

int countACKS() {
    auto count = 0;
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF && process.received_ack_from) {
            count++;
        }
    }
    return count;
}

void sendACKToEveryone() {
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF) {
            sendACK(process_id);
        }
    }
}

void checkREQ(int *indexes, MPI_Request *requests) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && REQS[process_id].response.dirty) {
            printf("[%s][%s][#%d] %s[%d] from #%d!\n", LRED("REQ"), LGREEN("RCV"), MYSELF,
                   LRED("Got REQ rooms="), REQS[process_id].response.data, process_id);
            listenFor(REQ, &REQS[process_id], process_id, &requests[process_id]);
            REQS[process_id].response.dirty = false;
            PROCESSES_MAP.at(process_id).rooms = REQS[process_id].response.data;
            PROCESSES_MAP.at(process_id).timestamp = REQS[process_id].response.timestamp;
            ROOMS_WANTED += REQS[process_id].response.data;
            if (REQS[process_id].response.timestamp < MY_DEMAND.timestamp) {
                sendACK(process_id); //fixme
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
                printf("[%s][%s][#%d] %s%d!\n", LBLUE("ACK"), LGREEN("RCV"), MYSELF,
                        LBLUE("Got ACK from #"), process_id);
                PROCESSES_MAP.at(process_id).received_ack_from = ACKS[process_id].response.data;
                ACKS_ACQUIRED += REQS[process_id].response.data;
            }
        }
    }
}

void checkRELEASE(int *indexes, MPI_Request *requests) {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && RELEASES[process_id].response.dirty) {
            printf("[%s][%s][#%d] %s=[%d] from #%d!\n", LMAG("RLS"), LGREEN("RCV"), MYSELF,
                    LMAG("Got RELEASE rooms"), RELEASES[process_id].response.data, process_id);
            listenFor(RELEASE, &RELEASES[process_id], process_id, &requests[RELEASE_OFFSET + process_id]);
            RELEASES[process_id].response.dirty = false;
            PROCESSES_MAP.at(process_id).rooms -= RELEASES[process_id].response.data;
            ROOMS_WANTED -= RELEASES[process_id].response.data;
        }
    }
}

void sendACK(int destination) {
    auto response = Response(REQS[destination].response.timestamp, true, true);
    printf("[%s][%s][#%d] %s%d\n", LBLUE("ACK"), LYELLOW("SND"), MYSELF,
            BLUE("Sending ACK to #"), destination);
    MPI_Send(&response, sizeof(struct Response), MPI_BYTE, destination, ACK, MPI_COMM_WORLD);
}

void sendRELEASE() {
    auto demand = Response(now(), MY_DEMAND.data, true);
    ROOMS_WANTED -= demand.data;
    MY_DEMAND = {};
    printf("[%s][%s][#%d] %s[%d]\n", LMAG("RLS"), LYELLOW("SND"), MYSELF,
           MAG("Sending RELEASE rooms="), demand.data);
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, RELEASE, MPI_COMM_WORLD);
        }
    }
}

void sendREQ() {
    auto demand = Response(now(), atLeastOne(), true);
    MY_DEMAND = demand;
    ROOMS_WANTED += demand.data;
    printf("[%s][%s][#%d] %s[%d][%lld]\n", LRED("REQ"), LYELLOW("SND"), MYSELF,
           RED("REQUESTING ROOMS="), demand.data, demand.timestamp);
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Response), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
            PROCESSES_MAP.at(destination).received_ack_from = false;
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
    return (rand() % MAX_ROOMS/2) + 1;
}

void sleepMillis(int millis) {
    this_thread::sleep_for(chrono::milliseconds(millis));
}

long long now() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

void timeout() {
    timed_mutex mtx;
    auto response = Response(now(), 1, true);
    mtx.lock();
    if (!mtx.try_lock_for(chrono::seconds(TIMEOUT))) {
        mtx.unlock();
        MPI_Send(&response, sizeof(struct Response), MPI_BYTE, MYSELF, ERROR_CODE, MPI_COMM_WORLD);
    }

}