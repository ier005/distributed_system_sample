#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_ntop & inrt_pton
#include <stdlib.h>
#include <unistd.h>

#include <ctime>
#include <iostream>
#include <iomanip>
#include <thread>


#include "workersmanager.h"
#include "taskqueue.h"
#include "master.pb.h"

#define DEFAULT_LISTEN_IP "0.0.0.0"
#define DEFAULT_WORKER_PORT 2345
#define DEFAULT_CLIENT_PORT 3456

#define WORKER_TASKQUEUE_SIZE 5
#define WORKER_HANDLERS_NUM 1

#define CLIENT_TASKQUEUE_SIZE 15
#define CLIENT_HANDLERS_NUM 5 

using namespace std;

//---------------------------------------
// Some Global Varibles and struct
//---------------------------------------
unsigned int client_tasks;

struct commandArgs_t {
	const char *ip;
	uint16_t worker_listen_port;
	uint16_t client_listen_port;
	bool verbose;
} commandArgs;

static const char *optString = "l:w:c:vh?";

struct handler_args {
	WorkersManager *wm;
	TaskQueue *tq;
};

//---------------------------------------
// set default value for args and parse command args
//---------------------------------------
void initArgs(){
	commandArgs.ip = DEFAULT_LISTEN_IP;
	commandArgs.worker_listen_port = DEFAULT_WORKER_PORT;
	commandArgs.client_listen_port = DEFAULT_CLIENT_PORT;
	commandArgs.verbose = false;
}

void disaplay_usage(char *cmd){
	cout<<"Usage: "<<cmd<<" <options>"<<endl;
	cout<<"Options:"<<endl;
	cout<<"    -l [ip addr]  listen IP"<<endl;
	cout<<"    -w [port]     port listening requests from Workers"<<endl;
	cout<<"    -c [port]     port listening requests from Clients"<<endl;
	cout<<"    -v            verbose info"<<endl;
}

void parse_args(int argc, char *argv[]){
	int opt, i;
	opt = getopt(argc, argv, optString);
	while(opt != -1){
		switch(opt){
			case 'l':
				commandArgs.ip = optarg;
				break;
			case 'w':
				i = atoi(optarg);
				if(i > 0)
					commandArgs.worker_listen_port = (uint16_t)i;
				break;
			case 'c':
				i = atoi(optarg);
				if(i > 0)
					commandArgs.client_listen_port = (uint16_t)i;
				break;
			case 'v':
				commandArgs.verbose = true;
				break;
			case 'h':
			case '?':
			default:
				disaplay_usage(argv[0]);
				exit(0);
		};
		opt = getopt(argc, argv, optString);
	}
}

//---------------------------------------
// listen and handle requests from workers
// and clients
//---------------------------------------
// bind and listen, return listen fd
int init_server(const char *ip, uint16_t port, int queue_len){
	int server_fd;
	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	int res = inet_pton(AF_INET, ip, &server_addr.sin_addr);
	server_addr.sin_port = htons(port);

	if((server_fd=socket(AF_INET,SOCK_STREAM,0))<0){
		cerr<<"init_server socket error"<<endl;
		return -1;
	}
	if(bind(server_fd,(struct sockaddr *)&server_addr,sizeof(struct sockaddr_in))<0){
		cerr<<"init_server bind error"<<endl;
		close(server_fd);
		return -1;
	}
	if(listen(server_fd, queue_len)<0){
		cerr<<"init_server listen error"<<endl;
		close(server_fd);
		return -1;
	}
	return server_fd;
}

//---------------------------------------
// for workers
//---------------------------------------
void worker_listener(TaskQueue *tq){
	int server_fd, conn_fd;
	int worker_queue_len = 5;
	struct sockaddr_in remote_addr;
	socklen_t sin_len = sizeof(remote_addr);
	ConnInfo ci;

	server_fd = init_server(commandArgs.ip, commandArgs.worker_listen_port, worker_queue_len);
	if(server_fd < 0){
		cerr<<"worker listener init error"<<endl;
		exit(-1);
	}
	while(1){
		if((conn_fd = accept(server_fd,(struct sockaddr *)&remote_addr, &sin_len))<0){
			cerr<<"worker_listener accept error"<<endl;
			perror("worker listen accept:");
			continue;
		}

		ci.remote_ip = remote_addr.sin_addr;
		ci.conn_fd = conn_fd;
		ci.srcType = Worker;
		tq->putTask(ci);
	}
}

void worker_handler(struct handler_args *args){
	WorkersManager *wm = args->wm;
	TaskQueue *tq = args->tq;
	ConnInfo ci;
	int conn_fd;
	uint32_t dataLength;
	unsigned char* recvBuf;
	ProtoMsg::WorkerInfo info;
	WorkerInfo w;
	bool parseSuccess;
	time_t update_time;

	while(1){
		if((tq->getTask(&ci)) < 0)
			break;
		conn_fd = ci.conn_fd;
		recv(conn_fd, &dataLength, sizeof(uint32_t), 0);
		//cout<<"recv datalength: "<<dataLength<<endl;
		recvBuf = (unsigned char *)malloc(dataLength);
		recv(conn_fd, recvBuf, dataLength, 0);
		parseSuccess = info.ParseFromArray(recvBuf, dataLength);
		free(recvBuf);

		w.ip = ci.remote_ip;
		w.port = (uint16_t)(info.port());
		time(&update_time);

		if((wm->updateWorker(&w, update_time))<0){
			wm->addNewWorker(&w, update_time);
			if(commandArgs.verbose){
				cout<<"[+]--- after worker update ---"<<endl;
				wm->printListInfo();
			}
		}

		close(conn_fd);
	}
}

//---------------------------------------
// for clients
//---------------------------------------
void client_listener(TaskQueue *tq){
	int server_fd, conn_fd;
	int client_queue_len = 5;
	struct sockaddr_in remote_addr;
	socklen_t sin_len = sizeof(remote_addr);
	ConnInfo ci;

	server_fd = init_server(commandArgs.ip, commandArgs.client_listen_port, client_queue_len);
	if(server_fd < 0){
		cerr<<"client listener init error"<<endl;
		exit(-1);
	}
	while(1){
		if((conn_fd = accept(server_fd,(struct sockaddr *)&remote_addr, &sin_len))<0){
			cerr<<"client_listener accept error"<<endl;
			perror("client listen accept:");
			continue;
		}

		ci.conn_fd = conn_fd;
		ci.srcType = Client;
		tq->putTask(ci);

		client_tasks++;
		//cout<<"client listener get tasks: "<< client_tasks <<endl;
	}
}

void client_handler(struct handler_args *args){
	WorkersManager *wm = args->wm;
	TaskQueue *tq = args->tq;
	ConnInfo ci;
	int conn_fd;
	uint32_t dataLength;
	unsigned char *buffer;
	ProtoMsg::ClientReq req;
	bool parseSuccess;
	ProtoMsg::WorkerAssign assign;
	WorkerInfo w;
	int assignSuccess;

	while(1){
		if((tq->getTask(&ci)) < 0)
			break;
		conn_fd = ci.conn_fd;
		//receive request msg from socket
		recv(conn_fd, &dataLength, sizeof(uint32_t), 0);
		buffer = (unsigned char *)malloc(dataLength);
		recv(conn_fd, buffer, dataLength, 0);
		parseSuccess = req.ParseFromArray(buffer, dataLength);
		free(buffer);

		if(req.req_index() > 1){
			w.ip.s_addr = req.last_assign_worker_ip();
			w.port = req.last_assign_worker_port();
			wm->disableWorker(&w);
			if(commandArgs.verbose){
				cout<<"[+]--- after worker disable ---"<<endl;
				wm->printListInfo();
			}
		}
		assignSuccess = wm->assignWorker(&w);

		// Send response to clients
 		if(assignSuccess < 0){
 			assign.set_worker_ip(0);
 			assign.set_worker_port(0);
 		} else {
 			assign.set_worker_ip(w.ip.s_addr);
 			assign.set_worker_port(w.port);
 		}

 		dataLength = assign.ByteSizeLong();
 		buffer = (unsigned char *)malloc(dataLength);
 		assign.SerializeToArray(buffer, dataLength);

 		send(conn_fd, &dataLength, sizeof(uint32_t), MSG_CONFIRM);
 		send(conn_fd, buffer, dataLength, MSG_CONFIRM);
 		free(buffer);

 		close(conn_fd);
 	}
}

//---------------------------------------
// find dead workers and clean them
//---------------------------------------
// scan_cycle: how often to do a whole scan (in seconds)
// ratio: how much times of scan will be carried out before a cleaning work
void worker_status_scanner(WorkersManager *wm, unsigned int scan_cycle, unsigned int ratio){
	unsigned int i;
	for(i=0; i<ratio; i++){
		sleep(scan_cycle);
		wm->markWorkerDead();
		if(commandArgs.verbose){
			cout<<"[+]--- after workers status scan ---"<<endl;
			wm->printListInfo();
		}
	}
	wm->cleanDeadWorkers();
	if(commandArgs.verbose){
		cout<<"[+]--- after dead workers clean ---"<<endl;
		wm->printListInfo();
	}
}

int main(int argc, char *argv[]){
	std::thread worker_listen_t, client_listen_t;
	std::thread worker_handle_t[WORKER_HANDLERS_NUM];
	std::thread client_handle_t[CLIENT_HANDLERS_NUM];

	WorkersManager wm = WorkersManager(30, 10);
	TaskQueue worker_tq(WORKER_TASKQUEUE_SIZE);
	//TaskQueue worker_tq = TaskQueue(WORKER_TASKQUEUE_SIZE);
	TaskQueue client_tq(CLIENT_TASKQUEUE_SIZE);

	initArgs();
	parse_args(argc, argv);
	cout<<"[+] Master Running ..."<<endl;
	cout<<"[+] Listen IP = "<<commandArgs.ip<<endl;
	cout<<"[+] Listening port for Workers = "<<commandArgs.worker_listen_port<<endl;
	cout<<"[+] Listening port for Clients = "<<commandArgs.client_listen_port<<endl;
	cout<<endl;

	client_tasks = 0;

	worker_listen_t = std::thread(&worker_listener, &worker_tq);
	client_listen_t = std::thread(&client_listener, &client_tq);

	struct handler_args worker_args = {.wm = &wm, .tq = &worker_tq};
	struct handler_args client_args = {.wm = &wm, .tq = &client_tq};
	int i;
	for(i=0; i < WORKER_HANDLERS_NUM; i++)
		worker_handle_t[i] = std::thread(&worker_handler, &worker_args);
	for(i=0; i < CLIENT_HANDLERS_NUM; i++)
		client_handle_t[i] = std::thread(&client_handler, &client_args);

	// if((pthread_create(&worker_tid, NULL, worker_listener, NULL))!=0){
	// 	cerr<<"main Worker listener create error"<<endl;
	// 	exit(-1);
	// }
	// if((pthread_create(&client_tid, NULL, client_listener, NULL))!=0){
	// 	cerr<<"main Client listener create error"<<endl;
	// 	exit(-1);
	// }
	while(1){
		worker_status_scanner(&wm, 45, 4);
	}
	return 0;
}
