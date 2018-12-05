#ifndef __TASK_QUEUE_H
#define __TASK_QUEUE_H

/*
* Task queue for master to handle messages or requests from 
* worker and client
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <mutex>
#include <condition_variable>
#include <queue>


using namespace std;

enum SourceType {
	Worker,
	Client
};

typedef struct ConnInfo{
	int conn_fd;
	struct in_addr remote_ip;
	SourceType srcType;
} ConnInfo;

class TaskQueue {
	public:
		TaskQueue(unsigned int queue_size);
		~TaskQueue();
		//return value less than 0 means error
		int putTask(ConnInfo task);
		//return value less than 0 means error
		int getTask(ConnInfo *task);
	private:
		std::mutex mtx;
		std::condition_variable consumer_cond;
		std::condition_variable producer_cond;
		unsigned int queue_size;
		std::queue<ConnInfo> tasks;
};

#endif