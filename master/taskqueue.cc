#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "taskqueue.h"

TaskQueue::TaskQueue(unsigned int queue_size){
	this->queue_size = queue_size;
}

TaskQueue::~TaskQueue(){
	this->queue_size = 0;
}

//return value less than 0 means error
int TaskQueue::putTask(ConnInfo task){
	std::unique_lock<std::mutex> lk(this->mtx);
	while(this->tasks.size()==this->queue_size)
		this->producer_cond.wait(lk);
	this->tasks.emplace(task);
	lk.unlock();
	this->consumer_cond.notify_one();
	return 0;
}

//return value less than 0 means error
int TaskQueue::getTask(ConnInfo *task){
	std::unique_lock<std::mutex> lk(this->mtx);
	while(this->tasks.empty())
		this->consumer_cond.wait(lk);
	*task = this->tasks.front();
	this->tasks.pop();
	lk.unlock();
	this->producer_cond.notify_one();
	return 0;
}

