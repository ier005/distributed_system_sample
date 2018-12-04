#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h> // inet_ntop & inrt_pton
#include <stdlib.h>
#include <unistd.h>

#include <ctime>
#include <string>
#include <iostream>
#include <iomanip>


#include "workersmanager.h"
#include "master.pb.h"

using namespace std;

//---------------------------------------
// Global Varibles
//---------------------------------------
WorkersManager wm(30, 10);

struct commandArgs_t {
	const char *ip;
	uint16_t worker_listen_port;
	uint16_t client_listen_port;
	bool verbose;
} commandArgs;

static const char *optString = "l:w:c:vh?";

//---------------------------------------
// set default value for args and parse command args
//---------------------------------------
void initArgs(){
	commandArgs.ip = "0.0.0.0";
	commandArgs.worker_listen_port = 2345;
	commandArgs.client_listen_port = 3456;
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
	//memset(&server_addr, 0, sizeof(struct sockaddr_in));
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
typedef struct _WorkerConnInfo{
	int conn_fd;
	struct in_addr ip;
} WorkerConnInfo;

void* worker_handler(void *args);

void* worker_listener(void *args){
	int server_fd, conn_fd;
	int worker_queue_len = 5;
	struct sockaddr_in remote_addr;
	socklen_t sin_len = sizeof(remote_addr);
	pthread_t tid;

	server_fd = init_server(commandArgs.ip, commandArgs.worker_listen_port, worker_queue_len);
	if(server_fd < 0)
		exit(-1);
	while(1){
		if((conn_fd = accept(server_fd,(struct sockaddr *)&remote_addr, &sin_len))<0){
			cerr<<"worker_listener accept error"<<endl;
			perror("worker listen accept:");
			continue;
		}
		WorkerConnInfo *p = new WorkerConnInfo;
		//cout<<"New Struct WorkConnInfo at "<<p<<endl;
		p->ip = remote_addr.sin_addr;
		p->conn_fd = conn_fd;
		if((pthread_create(&tid, NULL, worker_handler, p))!=0){
			cerr<<"worker_listener create handler error"<<endl;
			perror("worker_listener create handler: ");
			close(conn_fd);
			delete p;
			continue;
		}
	}
}

void* worker_handler(void *args){
	WorkerConnInfo *p = (WorkerConnInfo *)args;
	int conn_fd = p->conn_fd;
	uint32_t dataLength;
	unsigned char* recvBuf;
	ProtoMsg::WorkerInfo info;
	bool parseSuccess;
	WorkerInfo w;
	time_t update_time;

	pthread_detach(pthread_self());
	recv(conn_fd, &dataLength, sizeof(uint32_t), 0);
	//cout<<"recv datalength: "<<dataLength<<endl;
	recvBuf = (unsigned char *)malloc(dataLength);
	recv(conn_fd, recvBuf, dataLength, 0);

	// int i;
	// for(i=0; i<dataLength; i++)
	// 	cout<<i<<": "<<hex<<(unsigned int)recvBuf[i]<<endl;

	parseSuccess = info.ParseFromArray(recvBuf, dataLength);
	free(recvBuf);

	//cout<<"this msg port is: "<<info.has_port()<<" "<<dec<<info.port()<<endl;

	w.ip = p->ip;
	w.port = (uint16_t)(info.port());
	time(&update_time);
	if((wm.updateWorker(&w, update_time))<0){
		wm.addNewWorker(&w, update_time);
		if(commandArgs.verbose){
			cout<<"[+]--- after worker update ---"<<endl;
			wm.printListInfo();
		}
	}
	
	// if(commandArgs.verbose){
	// 	cout<<"[+]--- after worker update ---"<<endl;
	// 	wm.printListInfo();
	// }

	close(conn_fd);
	//cout<<"Delete Struct WorkConnInfo at "<<p<<endl;
	delete p;
}

//---------------------------------------
// for clients
//---------------------------------------
typedef struct _ClientConnInfo {
	int conn_fd;
	pthread_t tid;
} ClientConnInfo;

void* client_handler(void *args);

void* client_listener(void *args){
	int server_fd, conn_fd;
	int client_queue_len = 20;
	struct sockaddr_in remote_addr;
	socklen_t sin_len = sizeof(remote_addr);
	//pthread_t tid;

	server_fd = init_server(commandArgs.ip, commandArgs.client_listen_port, client_queue_len);
	if(server_fd < 0)
		exit(-1);
	while(1){
		if((conn_fd = accept(server_fd,(struct sockaddr *)&remote_addr, &sin_len))<0){
			cerr<<"client_listener accept error"<<endl;
			perror("client listen accept:");
			continue;
		}
		ClientConnInfo *p = (ClientConnInfo *)malloc(sizeof(ClientConnInfo));
		//cout<<"New Struct ClientConnInfo at "<<p<<endl;
		p->conn_fd = conn_fd;
		cout<<"client listener will create: "<<endl;
		if((pthread_create(&(p->tid), NULL, client_handler, p))!=0){
			cerr<<"client_listener create handler error"<<endl;
			perror("client_listener create handler: ");
			close(conn_fd);
			free(p);
			continue;
		}
		cout<<"client listener create success: "<<p->tid<<endl;
	}
}

void* client_handler(void *args){
	ClientConnInfo *p = (ClientConnInfo *)args;
	cout<<"enter client handler: "<<p->tid<<endl;
	int conn_fd = p->conn_fd;
	uint32_t dataLength;
	unsigned char *buffer;
	ProtoMsg::ClientReq req;
	bool parseSuccess;
	ProtoMsg::WorkerAssign assign;
	WorkerInfo w;
	int assignSuccess;

	pthread_detach(pthread_self());
	// Receive request from clients
	recv(conn_fd, &dataLength, sizeof(uint32_t), 0);
	//cout<<"recv dataLength: "<<dataLength<<endl;
	buffer = (unsigned char *)malloc(dataLength);
	recv(conn_fd, buffer, dataLength, 0);

	parseSuccess = req.ParseFromArray(buffer, dataLength);
	free(buffer);

	if(req.req_index() > 1){
		w.ip.s_addr = req.last_assign_worker_ip();
		w.port = req.last_assign_worker_port();
		wm.disableWorker(&w);
		if(commandArgs.verbose){
			cout<<"[+]--- after worker disable ---"<<endl;
			wm.printListInfo();
		}
 	}
 	assignSuccess = wm.assignWorker(&w);
 	// if(commandArgs.verbose){
 	// 	cout<<"[+]--- after worker assign ---"<<endl;
 	// 	wm.printListInfo();
 	// }

 	// Send response to clients
 	if(assignSuccess < 0){
 		assign.set_worker_ip(0);
 		assign.set_worker_port(0);
 		//cout<<"assign Fail!!!"<<endl;
 	} else {
 		assign.set_worker_ip(w.ip.s_addr);
 		assign.set_worker_port(w.port);
 		//cout<<"Succeeeeeeeeeeeeeess "<<w.ip.s_addr<<" "<<w.port<<endl;
 	}

 	dataLength = assign.ByteSizeLong();
 	buffer = (unsigned char *)malloc(dataLength);
 	assign.SerializeToArray(buffer, dataLength);

 	send(conn_fd, &dataLength, sizeof(uint32_t), MSG_CONFIRM);
 	send(conn_fd, buffer, dataLength, MSG_CONFIRM);
 	free(buffer);

 	close(conn_fd);
	//cout<<"Delete Struct ClientConnInfo at "<<p<<endl;
	free(p);
}

//---------------------------------------
// find dead workers and clean them
//---------------------------------------
// scan_cycle: how often to do a whole scan (in seconds)
// ratio: how much times of scan will be carried out before a cleaning work
void worker_status_scanner(unsigned int scan_cycle, unsigned int ratio){
	unsigned int i;
	for(i=0; i<ratio; i++){
		sleep(scan_cycle);
		wm.markWorkerDead();
		if(commandArgs.verbose){
			cout<<"[+]--- after workers status scan ---"<<endl;
			wm.printListInfo();
		}
	}
	wm.cleanDeadWorkers();
	if(commandArgs.verbose){
		cout<<"[+]--- after dead workers clean ---"<<endl;
		wm.printListInfo();
	}
}

int main(int argc, char *argv[]){
	pthread_t worker_tid, client_tid; 

	initArgs();
	parse_args(argc, argv);
	cout<<"[+] Master Running ..."<<endl;
	cout<<"[+] Listen IP = "<<commandArgs.ip<<endl;
	cout<<"[+] Listening port for Workers = "<<commandArgs.worker_listen_port<<endl;
	cout<<"[+] Listening port for Clients = "<<commandArgs.client_listen_port<<endl;
	cout<<endl;

	if((pthread_create(&worker_tid, NULL, worker_listener, NULL))!=0){
		cerr<<"main Worker listener create error"<<endl;
		exit(-1);
	}
	if((pthread_create(&client_tid, NULL, client_listener, NULL))!=0){
		cerr<<"main Client listener create error"<<endl;
		exit(-1);
	}
	while(1){
		worker_status_scanner(45, 4);
	}
	return 0;
}