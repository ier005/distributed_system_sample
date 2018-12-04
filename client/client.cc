/*************************************************************************
	> File Name: client.cpp
	> Author: 
	> Mail: 
	> Created Time: Sat 01 Dec 2018 11:53:21 PM PST
 ************************************************************************/

#include <iostream>
#include <netinet/in.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <string.h>  // for bzero
#include <unistd.h>  //for close
#include <arpa/inet.h> //for inet_addr
#include <string>
#include <thread>
#include <time.h>
#include <stdlib.h>

#include "client.pb.h"

using namespace std;

#define Thread_NUM 5
#define Recv_LEN 4

struct workerInfo
{
    bool isError;
    unsigned long ip;
    int port;
};

struct resultInfo
{
    bool isConnectError;
    bool isResponseError;
    double value;
};

struct in_addr Master_IP;
int Master_PORT;

workerInfo socket2master(const ClientReq& clientrequest) {
    workerInfo worker;
    worker.isError = false;
    worker.ip = 0;
    worker.port = 0;

    struct sockaddr_in client_addr;
    bzero(&client_addr, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htons(INADDR_ANY);
    client_addr.sin_port = htons(0);
    
    int client_socket = socket(AF_INET,SOCK_STREAM,0);
    if (client_socket < 0) {
        cout << "Create Socket Failed!" << endl;
        perror("Create Socket: ");
        pthread_exit(NULL);
    }
    
    if (bind(client_socket,(struct sockaddr*)&client_addr, sizeof(client_addr))){
        cout << "Client bind port failed!" << endl;
        pthread_exit(NULL);
    }
    
    struct sockaddr_in master_addr;
    bzero(&master_addr, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_addr = Master_IP;
    master_addr.sin_port = htons(Master_PORT);
    
    if (connect(client_socket,(struct sockaddr*)&master_addr, sizeof(master_addr))!=0){
        cout << "Master Connect failed! resend" << endl;
        worker.isError = true;
        close(client_socket);
        return worker;
    }

    //ready to send
    size_t size;
    char *buf_send;
    size = clientrequest.ByteSizeLong();
    buf_send = (char *)malloc(size + 4);
    memcpy(buf_send, &size, 4);
    clientrequest.SerializeToArray(buf_send + 4, size);
    if(send(client_socket,buf_send,size + 4,0)<0)
    {
        cout << "Send error" << endl;
        worker.isError = true;
        free(buf_send);
        close(client_socket);
        return worker;
    }

    //ready to recv
    WorkerAssign workerassign;
    int length = 0;
    char *buf_recv;
    if(recv(client_socket,&size,Recv_LEN,0)<=0){
        cout <<"[Error] Recv failed" << endl;
        worker.isError = true;
        close(client_socket);
        free(buf_send);
        return worker;
    }
    buf_recv = (char *)malloc(size);
    if(recv(client_socket,buf_recv,size,0)<=0){
        cout <<"[Error] Recv failed" << endl;
        worker.isError = true;
        goto master;
    }
    try {
        workerassign.ParseFromArray(buf_recv, size);
        //cout<<"ip: "<<workerassign.worker_ip()<<" port: "<<workerassign.worker_port()<<endl;
        worker.ip = workerassign.worker_ip();
        worker.port = workerassign.worker_port();

        if (worker.ip == 0 || worker.port == 0) {
            cout << "[Error] Something wrong with worker ip or port, send again" << endl;
            worker.isError = true;
            goto master;
        }
    }
    catch (string &e){
        cout << "[Error] Something wrong with master response, send again" << endl;
        worker.isError = true;
        goto master;
    }
master:
    close(client_socket);
    free(buf_send);
    free(buf_recv);
    return worker;
}

resultInfo socket2worker(workerInfo worker, const ClientTask& clienttask) {
    resultInfo result;
    result.isConnectError = false;
    result.isResponseError = false;
    result.value = 0;

    struct sockaddr_in client_addr;
    bzero(&client_addr, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = htons(INADDR_ANY);
    client_addr.sin_port = htons(0);
    
    int client_socket = socket(AF_INET,SOCK_STREAM,0);
    if (client_socket < 0) {
        cout << "Create Socket Failed!" << endl;
        pthread_exit(NULL);
    }
    
    if (bind(client_socket,(struct sockaddr*)&client_addr, sizeof(client_addr))){
        cout << "Client bind port failed!" << endl;
        pthread_exit(NULL);
    }
    
    struct sockaddr_in worker_addr;
    bzero(&worker_addr, sizeof(worker_addr));
    worker_addr.sin_family = AF_INET;
    worker_addr.sin_addr.s_addr = worker.ip;
    worker_addr.sin_port = htons(worker.port);
    
    if (connect(client_socket,(struct sockaddr*)&worker_addr, sizeof(worker_addr))!=0){
        char ip_str[INET_ADDRSTRLEN];
        const char* t = inet_ntop(AF_INET, &(worker.ip), ip_str, INET_ADDRSTRLEN);
        cout<< "Cannot connect to worker "<<ip_str<<" "<<worker.port<<endl;
        cout << "Worker Connect failed! resend to master" << endl;
        result.isConnectError = true;
        close(client_socket);
        return result;
    }

    //ready to send
    size_t size;
    char *buf_send;
    size = clienttask.ByteSizeLong();
    buf_send = (char *)malloc(size + 4);
    memcpy(buf_send, &size, 4);
    clienttask.SerializeToArray(buf_send + 4, size);
    if(send(client_socket,buf_send,size + 4,0)<0)
    {
        cout << "Send error" << endl;
        result.isResponseError = true;
        close(client_socket);
        free(buf_send);
        return result;
    }

    //ready to recv
    TaskResult taskresult;
    int length = 0;
    char *buf_recv;
    if(recv(client_socket,&size,Recv_LEN,0)<=0){
        cout << "[Error] Recv failed" << endl;
        result.isResponseError = true;
        close(client_socket);
        free(buf_send);
        return result;
    }
    buf_recv = (char *)malloc(size);
    if(recv(client_socket,buf_recv,size,0)<=0){
        cout << "[Error] Recv failed" << endl;
        result.isResponseError = true;
        goto worker;
    }
    try {
        taskresult.ParseFromArray(buf_recv, size);

        if (taskresult.is_error() == true) {
            cout << taskresult.err_msg() << endl;
            goto worker;
        }
        if (taskresult.result_case() == 2){
            //cout << taskresult.int_result() << endl;
            result.value = taskresult.int_result();
            goto worker;
        }
        else {
            //cout << taskresult.double_result() <<endl;
            result.value = taskresult.double_result();
            goto worker;
        }
    }
    catch (string &e){
        cout << "[Error] Something wrong with worker response, send again" << endl;
        result.isResponseError = true;
        goto worker;
    }
worker:   
    close(client_socket);
    free(buf_send);
    free(buf_recv);
    return result;
}

void task(int num, int index) {
    cout << "thread " << index << " is start." << endl;
    int checkpoint = 5; 
    double check_num1;
    double check_num2;
    char check_op;
    for (int i = 0; i < num; i++){
        ClientReq clientrequest;
        int req_index = 1;
        clientrequest.set_req_index(req_index);
        workerInfo worker = socket2master(clientrequest);
        // reconnect to master
        while (worker.isError){
            sleep(3);
            worker = socket2master(clientrequest);
        }

        ClientTask clienttask;
        srand(rand());
        if (rand()%2 == 1) { // is int
            int num_1 = rand() % 512 - 256;
            clienttask.set_int_num1(num_1);
            check_num1 = (double)num_1;
        }
        else { // is double
            double num_1 = rand() % 512 - 256 + rand() / double(RAND_MAX);
            clienttask.set_double_num1(num_1);
            check_num1 = num_1;
        }
        if (rand()%2 == 1) { // is int
            int num_2 = rand() % 512 - 256;
            clienttask.set_int_num2(num_2);
            check_num2 = (double)num_2;
        }
        else { // is double
            double num_2 = rand() % 512 - 256 + rand() / double(RAND_MAX);
            clienttask.set_double_num2(num_2);
            check_num2 = num_2;
        }
        int operation = rand()%4;
        switch(operation) {
            case 0:
                clienttask.set_op(ClientTask::ADD);
                check_op = '+';
                break;
            case 1:
                clienttask.set_op(ClientTask::SUB);
                check_op = '-';
                break;
            case 2:
                clienttask.set_op(ClientTask::MUL);
                check_op = '*';
                break;
            case 3:
                clienttask.set_op(ClientTask::DIV);
                check_op = '/';
                break;
        }
        
        resultInfo result = socket2worker(worker,clienttask);
        // reconnect to worker
        while ( result.isResponseError == true) {
            sleep(3);
            result = socket2worker(worker,clienttask);
        }

        // To beginning:reconnect everything 
        while ( result.isConnectError == true ) {
            cout << "**************reconnect to master**************"<<endl;
            sleep(3);
            req_index++;
            clientrequest.set_req_index(req_index);
            clientrequest.set_last_assign_worker_ip(worker.ip);
            clientrequest.set_last_assign_worker_port(worker.port);
            worker = socket2master(clientrequest);
            // reconnect to master
            while (worker.isError){
                sleep(3);
                worker = socket2master(clientrequest);
            }
            result = socket2worker(worker,clienttask);
            // reconnect to worker
            while ( result.isResponseError == true) {
                sleep(3);
                result = socket2worker(worker,clienttask);
            }
        }
        
        if (((double)i/num) * 100 > checkpoint ) {
            cout << "thread " << index << " "<< checkpoint <<"% is finished." << endl;
            checkpoint += 5;
            cout << "CHECK POINT ! The Arith is : " << check_num1 <<" " << check_op << " " << check_num2 << " = "<< result.value << endl;
        }
        sleep(0.01);
    }
    cout << "thread " << index << " is finished." << endl;
    google::protobuf::ShutdownProtobufLibrary();
}

int main(int argc, char** argv) {
    if (argc != 4) {
        cout << "Usage: Master_NAME Master_PORT NUM" << endl;
        return 0;
    }
    
    int num = atoi(argv[3]);
    Master_PORT = atoi(argv[2]);
    struct hostent *h;
    // master addr
    h = gethostbyname(argv[1]);
    if (h == NULL) {
        cout << "[ERROR] MASTER_NAME is wrong!" << endl;
        exit(-1);
    }
    Master_IP = *(struct in_addr*)h->h_addr;
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    
    for(int i = 0; i< Thread_NUM; i++){
        thread t(task, floor(num/Thread_NUM), i);
        t.detach();
    }
    cout << "Enter ! to exit" << endl;
    while (char c = getchar()) {
        if (c == '!')
            break;
    }
    return 0;
}
