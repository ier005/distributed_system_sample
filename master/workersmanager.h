#ifndef _WORKER_MANAGER_H
#define _WORKER_MANAGER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include <ctime>

typedef struct _WorkerInfo {
	struct in_addr ip;
	uint16_t port;
} WorkerInfo;

// Alive
// Unaccessible: this worker may be alive, but client cannot connect to it
// Dead: this worker hasn't sent heartbeat message for a while (such as for 2min)
enum WorkerStatus {
	Alive,
	Unaccessible,
	Dead
};

typedef struct Node {
	WorkerInfo info;
	WorkerStatus status;
	time_t update_time;
	int lifeValue;
	struct Node *prev;
	struct Node *next;
} WorkerNode;

class WorkersManager {
	public:
		WorkersManager(double heartbeat_interval, int maxLifeValue);
		~WorkersManager();
		// Update one work's update_time
		// return < 0 indicates this worker doesn't exist
		int updateWorker(WorkerInfo *w, time_t update_time);
		// Add a new worker
		void addNewWorker(WorkerInfo *w, time_t update_time);
		// Assign a worker for a client
		int assignWorker(WorkerInfo *assigned_worker);
		// Change some worker to unaccessible status from alive status
		void disableWorker(WorkerInfo *w);
		// If the interval between current time and update time exceeds interval limit,
		// mark the worker as dead (status -> DEAD)
		void markWorkerDead();
		// Delete dead workers
		void cleanDeadWorkers();
		// print current info about WorkerNode List 
		void printListInfo();
	private:
		// The root of WorkerNode double-linked list
		WorkerNode root;
		// To trace next worker to be assigned
		WorkerNode *next_assign;
		// HeartBeat interval for workers (in seconds)
		double HeartBeatInterval;
		// Max Life Value for workers
		int MaxLifeValue;
		// Read-Write lock for WorkerNode list operations
		pthread_rwlock_t rwlock;
		// mutex for assign worker for client requests
		pthread_mutex_t assign_mutex;
};

#endif