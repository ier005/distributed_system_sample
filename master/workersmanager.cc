#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h> // inet_ntop & inrt_pton

#include <ctime>
#include <string>
#include <iostream>
#include <iomanip>

#include "workersmanager.h"

using namespace std;

WorkersManager::WorkersManager(double heartbeat_interval, int maxLifeValue){
	root.next = &root;
	root.prev = &root;
	next_assign = &root;
	HeartBeatInterval = heartbeat_interval;
	MaxLifeValue = maxLifeValue;
	pthread_rwlock_init(&rwlock, NULL);
	pthread_mutex_init(&assign_mutex, NULL);
	// cout<<"init"<<endl;
	// cout<<"root address: "<<&root<<endl;
	// cout<<"root prev: "<<root.prev<<endl;
	// cout<<"root next: "<<root.next<<endl;
}

WorkersManager::~WorkersManager(){
	pthread_mutex_destroy(&assign_mutex);
	pthread_rwlock_destroy(&rwlock);
	WorkerNode *tmp;
	while(root.next!=&root){
		tmp = root.next;
		tmp->prev->next = tmp->next;
		tmp->next->prev = tmp->prev;
		delete tmp;
	}
	next_assign = NULL;
	//cout<<"fini"<<endl;
}

static int compareWorkInfo(WorkerInfo *a, WorkerInfo *b){
	uint32_t a_ip, b_ip;
	a_ip = *(uint32_t *)&((a->ip));
	b_ip = *(uint32_t *)&((b->ip));
	if(a_ip > b_ip)
		return 1;
	if(a_ip == b_ip && a->port > b->port)
		return 1;
	if(a_ip == b_ip && a->port == b->port)
		return 0;
	else
		return -1;
}

int WorkersManager::updateWorker(WorkerInfo *w, time_t update_time){
	WorkerNode *tmp;
	int find_flag = 0;
	int cmp_result;
	// Read lock
	pthread_rwlock_rdlock(&rwlock);
	for(tmp=root.next; tmp!=&root; tmp=tmp->next){
		cmp_result = compareWorkInfo(w, &(tmp->info));
		if(cmp_result == 0){
			find_flag = 1;
			tmp->update_time = update_time;
			tmp->status = Alive;
			tmp->lifeValue = MaxLifeValue;
		}
		if(cmp_result <= 0)
			break;
	}
	pthread_rwlock_unlock(&rwlock);
	if(find_flag)
		return 1; // Update successfully
	else
		return -1; // Worker not found
}

void WorkersManager::addNewWorker(WorkerInfo *w, time_t update_time){
	WorkerNode *tmp, *p;
	int cmp_result;
	// Write lock
	pthread_rwlock_wrlock(&rwlock);
	p = NULL;
	for(tmp=root.next; tmp!=&root; tmp=tmp->next){
		cmp_result = compareWorkInfo(w, &(tmp->info));
		if(cmp_result < 0)
			p = tmp;
		if(cmp_result <= 0)
			break;
	}
	if(!p && tmp==&root)
		p = &root;
	if(p){
		WorkerNode *new_node;
		if((new_node = new WorkerNode)){
			new_node->info = *w;
			new_node->update_time = update_time;
			new_node->status = Alive;
			new_node->lifeValue = MaxLifeValue;
			new_node->prev = p->prev;
			new_node->next = p;
			p->prev->next = new_node;
			p->prev = new_node;
		}
	}
	pthread_rwlock_unlock(&rwlock);
}

int WorkersManager::assignWorker(WorkerInfo *assigned_worker){
	WorkerNode *tmp;
	int anyAliveWorker = -1;
	int fullCycle = 0;
	pthread_mutex_lock(&assign_mutex);
	// Read lock
	pthread_rwlock_rdlock(&rwlock);
	tmp = next_assign;
	while(tmp == &root || tmp->status != Alive){
		tmp = tmp->next;
		if(tmp == next_assign){
			fullCycle = 1;
			break;
		}
	}
	if(fullCycle){
		// after a whole lookup cycle, find no alive worker
		// assigned_worker.ip.s_addr = 0xffffffff;
		// assigned_worker.port = 0xffff;
		anyAliveWorker = -1;
	} else {
		anyAliveWorker = 1;
		assigned_worker->ip = tmp->info.ip;
		assigned_worker->port = tmp->info.port;
	}
	next_assign = tmp->next;
	pthread_rwlock_unlock(&rwlock);
	pthread_mutex_unlock(&assign_mutex);
	return anyAliveWorker;
}

void WorkersManager::disableWorker(WorkerInfo *w){
	WorkerNode *tmp;
	int cmp_result;
	// Read lock
	pthread_rwlock_rdlock(&rwlock);
	for(tmp=root.next; tmp!=&root; tmp=tmp->next){
		cmp_result = compareWorkInfo(w, &(tmp->info));
		if(cmp_result == 0){
			tmp->lifeValue--;
			if(tmp->lifeValue <= 0)
				tmp->status = Unaccessible;
		}
		if(cmp_result <= 0)
			break;
	}
	pthread_rwlock_unlock(&rwlock);
}

void WorkersManager::markWorkerDead(){
	WorkerNode *tmp;
	time_t current_time;
	// Read lock
	pthread_rwlock_rdlock(&rwlock);
	time(&current_time);
	for(tmp=root.next; tmp!=&root; tmp=tmp->next){
		if(difftime(current_time, tmp->update_time) > HeartBeatInterval)
			tmp->status = Dead;
	}
	pthread_rwlock_unlock(&rwlock);
}

void WorkersManager::cleanDeadWorkers(){
	WorkerNode *tmp, *tmp_next;
	// Write lock
	pthread_rwlock_wrlock(&rwlock);
	tmp = root.next;
	while(tmp!=&root){
		if(tmp->status == Dead){
			tmp->prev->next = tmp->next;
			tmp->next->prev = tmp->prev;
			tmp_next = tmp->next;
			if(next_assign == tmp)
				next_assign = tmp->next;
			delete tmp;
			tmp = tmp_next;
		}
		else
			tmp = tmp->next;
	}
	pthread_rwlock_unlock(&rwlock);
}

void WorkersManager::printListInfo(){
	WorkerNode *tmp;
	char ip_str[INET_ADDRSTRLEN];
	time_t current_time;
	string status;
	// cout<<"in print"<<endl;
	// cout<<"root address: "<<&root<<endl;
	// cout<<"root prev: "<<root.prev<<endl;
	// cout<<"root next: "<<root.next<<endl;
	// Read lock
	pthread_rwlock_rdlock(&rwlock);
	time(&current_time);
	if(next_assign == &root)
		cout<<"-->| NONE"<<endl;
	else
		cout<<"   | NONE"<<endl;
	for(tmp=root.next; tmp!=&root; tmp=tmp->next){
		// cout<<"root address: "<<&root<<endl;
		// cout<<"tmp address: "<<tmp<<endl;
		// cout<<"tmp prev: "<<tmp->prev<<endl;
		// cout<<"tmp next: "<<tmp->next<<endl;
		const char* t = inet_ntop(AF_INET, &((tmp->info).ip), ip_str, INET_ADDRSTRLEN);
		uint16_t port = (tmp->info).port;
		double interval = difftime(current_time, tmp->update_time);
		switch(tmp->status){
			case Alive: status = "Alive"; break;
			case Unaccessible: status = "Unaccessible"; break;
			case Dead: status = "Dead"; break;
			default: status = "Unknown status";
		};
		int lifeValue = tmp->lifeValue;
		if(next_assign == tmp){
			cout<<"-->|"<<setw(15)<<ip_str<<"|"<<setw(5)<<port<<"|";
			cout<<setw(4)<<setprecision(2)<<interval<<"s|"<<setw(4)<<lifeValue<<"|"<<status<<endl;
		} else{
			cout<<"   |"<<setw(15)<<ip_str<<"|"<<setw(5)<<port<<"|";
			cout<<setw(4)<<setprecision(2)<<interval<<"s|"<<setw(4)<<lifeValue<<"|"<<status<<endl;
		}
	}
	cout<<endl;
	pthread_rwlock_unlock(&rwlock);
}