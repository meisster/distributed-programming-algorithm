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
#define E_ACK 4
#define E_REQ 5
#define E_RELEASE 6
#define ERROR_CODE 500

#define MAX_ROOMS 10
#define TIMEOUT 8
#define ELEVATORS 2

struct Message {
    Message() = default;

    Message(long long int timestamp, int data, bool dirty) : timestamp(timestamp), data(data), dirty(dirty) {}

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

    bool has_older_timestamp(Message *my_demand, int process_id, int my_id) const {
        if (this->timestamp < my_demand->timestamp && this->timestamp != 0) {
            return true;
        } else if (this->timestamp == my_demand->timestamp) {
            printf("[#%d][INFO] Timestamps equal! Prioritizing lower ID [%d] > [%d]\n", my_id, process_id, my_id);
            return process_id > my_id;
        } else {
            return false;
        };
    }

    bool received_ACK() const {
        return this->received_ack_from;
    }
};

static int MYSELF;
static int SIZE;
static bool CAN_OCCUPY_ROOMS;
static bool CAN_OCCUPY_ELEVATOR;
static int ACKS_OFFSET;
static int RELEASE_OFFSET;
static int E_REQ_OFFSET;
static int E_ACKS_OFFSET;
static int E_RELEASE_OFFSET;

static int REQUESTS_FINISHED_COUNT = 0;
static int ROOMS_WANTED = 0;
static int ACKS_ACQUIRED = 0;
static int ELEVATORS_WANTED = 0;
static int ELEVATORS_ACK_ACQUIRED = 0;
static int ERROR;

static Message MY_REQUEST;
static Message E_MY_REQUEST;
static Message ERROR_RESPONSE = {};
static std::map<int, ProcessData> PROCESSES_MAP;
static std::map<int, ProcessData> E_PROCESSES_MAP;
std::vector<Message> REQS;
std::vector<Message> ACKS;
std::vector<Message> RELEASES;
std::vector<Message> E_REQS;
std::vector<Message> E_ACKS;
std::vector<Message> E_RELEASES;

std::vector<MPI_Request> REQUESTS;

long long now();

int at_least_one();

void init(std::vector<Message> *messages);

void sleep_millis(int millis);

void check_REQ();

void check_ACK();

void check_RELEASE();

bool error_occurred();

void send_ACK(int destination);

void init_processes_map();

void listen_for(int TAG, Message *message, int process_id, MPI_Request *request);

void send_REQ();

void timeout();

bool can_occupy_rooms();

void send_RELEASE();

void init_requests();

void send_ACK_to_everyone();

int count_ACKS();

void initialize();

void prepare_timeout_thread();

void check_E_REQ();

void send_E_ACK(int process_id);

void send_E_REQ();

void check_E_ACK();

void check_E_RELEASE();

bool can_occupy_elevator();

void send_E_RELEASE();

void wait_for_elevator_access();

void use_elevator();

void use_room();

void leave_room();

void wait_for_room_access();

void send_E_ACK_to_everyone();

void leave_elevator();

void init_e_processes_map();

using namespace std;

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &MYSELF);
    MPI_Comm_size(MPI_COMM_WORLD, &SIZE);
    initialize();
    prepare_timeout_thread();

    // send initial requests to all processes
    send_REQ();

    // start handles for responses
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            listen_for(REQ, &REQS[process_id], process_id, &REQUESTS[process_id]);
            listen_for(ACK, &ACKS[process_id], process_id, &REQUESTS[ACKS_OFFSET + process_id]);
            listen_for(RELEASE, &RELEASES[process_id], process_id, &REQUESTS[RELEASE_OFFSET + process_id]);
            listen_for(E_REQ, &E_REQS[process_id], process_id, &REQUESTS[E_REQ_OFFSET + process_id]);
            listen_for(E_ACK, &E_ACKS[process_id], process_id, &REQUESTS[E_ACKS_OFFSET + process_id]);
            listen_for(E_RELEASE, &E_RELEASES[process_id], process_id, &REQUESTS[E_RELEASE_OFFSET + process_id]);
        }
    }

    // main loop
    while (!error_occurred()) {
        wait_for_room_access();
        wait_for_elevator_access();
        use_elevator(); // going down the elevator
        leave_elevator();
        use_room(); // chill in the solitary room
        wait_for_elevator_access(); // we need the elevator again
        use_elevator(); // going down the elevator
        leave_elevator();
        leave_room();
    }

    printf("[#%d] %s\n", MYSELF, LRED("Error has occurred (probably timeout), shutting down!"));
    MPI_Finalize();
}

void wait_for_room_access() {
    int indexes[(SIZE * 6) + 1];
    while (!error_occurred()) {
        MPI_Waitsome((SIZE * 6) + 1, REQUESTS.data(), &REQUESTS_FINISHED_COUNT, indexes, MPI_STATUSES_IGNORE);
        check_ACK();
        check_RELEASE();
        check_REQ();
        if (can_occupy_rooms()) break;
        check_E_REQ();
    }
}

void wait_for_elevator_access() {
    int indexes[(SIZE * 6) + 1];
    send_E_REQ();
    send_E_ACK_to_everyone();
    while (!error_occurred()) {
        MPI_Waitsome((SIZE * 6) + 1, REQUESTS.data(), &REQUESTS_FINISHED_COUNT, indexes, MPI_STATUSES_IGNORE);
        check_E_REQ();
        check_E_ACK();
        check_E_RELEASE();
        if (can_occupy_elevator()) break;
    }
}

bool can_occupy_rooms() {
    bool iCanGoIn = false;
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            iCanGoIn = PROCESSES_MAP.at(process_id).has_older_timestamp(&MY_REQUEST, process_id, MYSELF) ||
                       PROCESSES_MAP.at(process_id).received_ACK();
            if (!iCanGoIn) break;
        }
    }
    if (ROOMS_WANTED - ACKS_ACQUIRED <= MAX_ROOMS && iCanGoIn) {
        printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Permission to enter room granted!"));
        CAN_OCCUPY_ROOMS = true;
        return true;
    } else {
        CAN_OCCUPY_ROOMS = false;
        return false;
    }
//    if (ROOMS_WANTED - ACKS_ACQUIRED <= MAX_ROOMS && iCanGoIn && count_ACKS() >= SIZE - ELEVATORS) {
//        printf("[%s][%s][#%d] %s=[%d] with %d ACKS!\n", YELL("OCC"), LGREEN("SCC"), MYSELF,
//               LGREEN("I have taken the rooms"), MY_REQUEST.data, count_ACKS());
//        sleep_millis(5000); // enjoy your time in isolation!
//        send_RELEASE();
//        sleep_millis(500); // think about sending new request
//        send_REQ();
//        send_ACK_to_everyone(); // put myself at the end the line
//    }
}

bool can_occupy_elevator() {
    bool iCanGoIn = false;
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF) {
            iCanGoIn = E_PROCESSES_MAP.at(process_id).has_older_timestamp(&E_MY_REQUEST, process_id, MYSELF) ||
                       E_PROCESSES_MAP.at(process_id).received_ACK();
            if (!iCanGoIn) break;
        }
    }
    if (ELEVATORS_WANTED - ELEVATORS_ACK_ACQUIRED <= ELEVATORS && iCanGoIn) {
        printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Permission to enter elevator granted!"));
        return true;
    } else return false;
}

void leave_room() {
    printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Out of the room!"));
    send_RELEASE();
    send_REQ();
    send_ACK_to_everyone(); // put myself at the end the line
}

void use_room() {
    printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Using the room, be right back in 2 seconds!"));
    sleep_millis(2000);
}

void use_elevator() {
    printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Using the elevator!"));
    sleep_millis(2000);
}

void leave_elevator() {
    printf("[INFO][#%d] %s\n", MYSELF, LGREEN("Leaving the elevator!"));
    send_E_RELEASE();
}

void check_E_RELEASE() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && E_RELEASES[process_id].dirty) {
            printf("[%s][%s][#%d] %s= from #%d!\n", LMAG("ERL"), LGREEN("RCV"), MYSELF,
                   LMAG("Got E_RELEASE"), process_id);
            listen_for(E_RELEASE, &E_RELEASES[process_id], process_id, &REQUESTS[E_RELEASE_OFFSET + process_id]);
            E_RELEASES[process_id].dirty = false;
            E_PROCESSES_MAP.at(process_id).rooms -= E_RELEASES[process_id].data;
            ELEVATORS_WANTED--;
        }
    }
}

void check_E_ACK() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && E_ACKS[process_id].dirty) {
            listen_for(ACK, &E_ACKS[process_id], process_id, &REQUESTS[E_ACKS_OFFSET + process_id]);
            E_ACKS[process_id].dirty = false;
            if (E_ACKS[process_id].timestamp == E_MY_REQUEST.timestamp) {
                printf("[%s][%s][#%d] %s%d!\n", LBLUE("ECK"), LGREEN("RCV"), MYSELF,
                       LBLUE("Got E_ACK from #"), process_id);
                E_PROCESSES_MAP.at(process_id).received_ack_from = E_ACKS[process_id].data;
                ELEVATORS_ACK_ACQUIRED++;
            }
        }
    }
}

void check_E_REQ() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && E_REQS[process_id].dirty) {
            printf("[%s][%s][#%d] %s[%d] from #%d!\n", LRED("ERQ"), LGREEN("RCV"), MYSELF,
                   LRED("Got E_REQ="), E_REQS[process_id].data, process_id);

            listen_for(E_REQ, &E_REQS[process_id], process_id, &REQUESTS[E_REQ_OFFSET + process_id]); // reactivate request handle
            E_REQS[process_id].dirty = false; // mark response as processed
            E_PROCESSES_MAP.at(process_id).rooms = E_REQS[process_id].data;
            E_PROCESSES_MAP.at(process_id).timestamp = E_REQS[process_id].timestamp;
            ELEVATORS_WANTED++;
            if (E_REQS[process_id].timestamp < E_MY_REQUEST.timestamp || !CAN_OCCUPY_ROOMS) {
                send_E_ACK(process_id);
            }
        }
    }
}

void send_E_REQ() {
    auto demand = Message(now(), 1, true);
    E_MY_REQUEST = demand;
    ELEVATORS_WANTED++;
    printf("[%s][%s][#%d] %s[%lld]\n", LRED("REQ"), LYELLOW("SND"), MYSELF,
           RED("REQUESTING ELEVATOR "), demand.timestamp);
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Message), MPI_BYTE, destination, E_REQ, MPI_COMM_WORLD);
            E_PROCESSES_MAP.at(destination).received_ack_from = false;
        }
    }
    ELEVATORS_ACK_ACQUIRED = 0; // since this is a new request, we need to reset ACKS
}

void send_E_ACK(int process_id) {
    // send ACK to latest received request from *destination*
    auto response = Message(E_REQS[process_id].timestamp, true, true);
    printf("[%s][%s][#%d] %s%d\n", LBLUE("ECK"), LYELLOW("SND"), MYSELF,
           BLUE("Sending E_ACK to #"), process_id);
    MPI_Send(&response, sizeof(struct Message), MPI_BYTE, process_id, E_ACK, MPI_COMM_WORLD);
}

void send_E_RELEASE() {
    auto demand = Message(now(), E_MY_REQUEST.data, true);
    ELEVATORS_WANTED--;
    E_MY_REQUEST = {};
    printf("[%s][%s][#%d] %s\n", LMAG("ERL"), LYELLOW("SND"), MYSELF, MAG("Sending E_RELEASE"));
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Message), MPI_BYTE, destination, E_RELEASE, MPI_COMM_WORLD);
        }
    }
}

int count_ACKS() {
    auto count = 0;
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF && process.received_ack_from) {
            count++;
        }
    }
    return count;
}

void send_ACK_to_everyone() {
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF) {
            send_ACK(process_id);
        }
    }
}

void send_E_ACK_to_everyone() {
    for (auto const&[process_id, process] : PROCESSES_MAP) {
        if (process_id != MYSELF) {
            send_E_ACK(process_id);
        }
    }
}

void check_REQ() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && REQS[process_id].dirty) {
            printf("[%s][%s][#%d] %s[%d] from #%d!\n", LRED("REQ"), LGREEN("RCV"), MYSELF,
                   LRED("Got REQ rooms="), REQS[process_id].data, process_id);

            listen_for(REQ, &REQS[process_id], process_id, &REQUESTS[process_id]); // reactivate request handle
            REQS[process_id].dirty = false; // mark response as processed
            PROCESSES_MAP.at(process_id).rooms = REQS[process_id].data;
            PROCESSES_MAP.at(process_id).timestamp = REQS[process_id].timestamp;
            ROOMS_WANTED += REQS[process_id].data;
            if (REQS[process_id].timestamp < MY_REQUEST.timestamp) {
                send_ACK(process_id);
            }
        }
    }
}

void check_ACK() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && ACKS[process_id].dirty) {
            listen_for(ACK, &ACKS[process_id], process_id, &REQUESTS[ACKS_OFFSET + process_id]);
            ACKS[process_id].dirty = false;
            if (ACKS[process_id].timestamp == MY_REQUEST.timestamp) {
                printf("[%s][%s][#%d] %s%d!\n", LBLUE("ACK"), LGREEN("RCV"), MYSELF,
                       LBLUE("Got ACK from #"), process_id);
                PROCESSES_MAP.at(process_id).received_ack_from = ACKS[process_id].data;
                ACKS_ACQUIRED += REQS[process_id].data;
            }
        }
    }
}

void check_RELEASE() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        if (process_id != MYSELF && RELEASES[process_id].dirty) {
            printf("[%s][%s][#%d] %s=[%d] from #%d!\n", LMAG("RLS"), LGREEN("RCV"), MYSELF,
                   LMAG("Got RELEASE rooms"), RELEASES[process_id].data, process_id);
            listen_for(RELEASE, &RELEASES[process_id], process_id, &REQUESTS[RELEASE_OFFSET + process_id]);
            RELEASES[process_id].dirty = false;
            PROCESSES_MAP.at(process_id).rooms -= RELEASES[process_id].data;
            ROOMS_WANTED -= RELEASES[process_id].data;
        }
    }
}

void send_ACK(int destination) {
    // send ACK to latest received request from *destination*
    auto response = Message(REQS[destination].timestamp, true, true);
    printf("[%s][%s][#%d] %s%d\n", LBLUE("ACK"), LYELLOW("SND"), MYSELF,
           BLUE("Sending ACK to #"), destination);
    MPI_Send(&response, sizeof(struct Message), MPI_BYTE, destination, ACK, MPI_COMM_WORLD);
}

void send_RELEASE() {
    auto demand = Message(now(), MY_REQUEST.data, true);
    ROOMS_WANTED -= demand.data;
    MY_REQUEST = {};
    printf("[%s][%s][#%d] %s[%d]\n", LMAG("RLS"), LYELLOW("SND"), MYSELF,
           MAG("Sending RELEASE rooms="), demand.data);
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Message), MPI_BYTE, destination, RELEASE, MPI_COMM_WORLD);
        }
    }
}

void send_REQ() {
    auto demand = Message(now(), at_least_one(), true);
    MY_REQUEST = demand;
    ROOMS_WANTED += demand.data;
    printf("[%s][%s][#%d] %s[%d][%lld]\n", LRED("REQ"), LYELLOW("SND"), MYSELF,
           RED("REQUESTING ROOMS="), demand.data, demand.timestamp);
    for (int destination = 0; destination < SIZE; destination++) {
        if (destination != MYSELF) {
            MPI_Send(&demand, sizeof(struct Message), MPI_BYTE, destination, REQ, MPI_COMM_WORLD);
            PROCESSES_MAP.at(destination).received_ack_from = false;
        }
    }
    ACKS_ACQUIRED = 0; // since this is a new request, we need to reset ACKS
}

void listen_for(int TAG, Message *message, int process_id, MPI_Request *request) {
    MPI_Irecv(message,
              sizeof(struct Message),
              MPI_BYTE, process_id, TAG, MPI_COMM_WORLD,
              request);
}

void init_processes_map() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        auto d = ProcessData();
        PROCESSES_MAP.insert({process_id, d});
    }
}


void init_e_processes_map() {
    for (int process_id = 0; process_id < SIZE; process_id++) {
        auto d = ProcessData();
        E_PROCESSES_MAP.insert({process_id, d});
    }
}

void init_requests() {
    REQUESTS.resize(SIZE * 6 + 1);
    for (int i = 0; i < (SIZE * 6) + 1; i++) {
        REQUESTS[i] = MPI_REQUEST_NULL;
    }
}

void init(vector<Message> *messages) {
    messages->resize(SIZE);
    for (int process_id = 0; process_id < SIZE; process_id++) {
        messages->at(process_id) = {};
    }
}

int at_least_one() {
//    return (rand() % MAX_ROOMS) + 1;
    return 1;
}

void sleep_millis(int millis) {
    this_thread::sleep_for(chrono::milliseconds(millis));
}

long long now() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

bool error_occurred() {
    return REQUESTS[ERROR] == MPI_REQUEST_NULL || REQUESTS_FINISHED_COUNT == MPI_UNDEFINED;
}

void timeout() {
    timed_mutex mtx;
    auto response = Message(now(), 1, true);
    mtx.lock();
    if (!mtx.try_lock_for(chrono::seconds(TIMEOUT))) { // eliminates active wait
        mtx.unlock();
        MPI_Send(&response, sizeof(struct Message), MPI_BYTE, MYSELF, ERROR_CODE, MPI_COMM_WORLD);
    }
}

void prepare_timeout_thread() {
    listen_for(ERROR_CODE, &ERROR_RESPONSE, MPI_ANY_SOURCE, &REQUESTS[ERROR]);
    thread(timeout).detach(); // will send error message and terminate
}

void initialize() {
    srand((unsigned) time(nullptr) + MYSELF);

    ACKS_OFFSET =           SIZE;
    RELEASE_OFFSET =    2 * SIZE;
    E_REQ_OFFSET =      3 * SIZE;
    E_ACKS_OFFSET =     4 * SIZE;
    E_RELEASE_OFFSET =  5 * SIZE;
    ERROR =             6 * SIZE; // index of error message

    init(&REQS);
    init(&ACKS);
    init(&RELEASES);
    init(&E_REQS);
    init(&E_ACKS);
    init(&E_RELEASES);
    init_requests();
    init_processes_map();
    init_e_processes_map();
}

