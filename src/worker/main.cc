#include <iostream>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>

#include "worker.pb.h"
#include "thread_pool.h"

#define LISTEN_ADDR "0.0.0.0"
#define LISTEN_NUM 5
#define HB_INTERVAL 30
#define RECV_BUF_SIZE 1024
#define POOL_SIZE 5
#define QUEUE_SIZE 10

using namespace std;

unsigned short listen_port;
void *heart_beat(void *);
void *work_func(void *);

int main(int argc, char **argv) {
    if (argc != 4) {
        cout << "Please use: ./worker MASTER_NAME MASTER_PORT LISTEN_PORT" << endl;
        return 0;
    }

    int listen_fd;
    struct sockaddr_in listen_addr;
    struct hostent *h;
    unsigned short master_port;
    struct in_addr master_ip;
    struct sockaddr_in master_addr;
    pthread_t hb_thread;

    listen_port = atoi(argv[3]);
    if (listen_port <= 0) {
        cout << "[ERROR] LISTEN_PORT is wrong!" << endl;
        exit(-1);
    }

    // master addr
    h = gethostbyname(argv[1]);
    if (h == NULL) {
        cout << "[ERROR] MASTER_NAME is wrong!" << endl;
        exit(-1);
    }
    master_ip = *(struct in_addr*)h->h_addr;
    master_port = atoi(argv[2]);
    if (master_port <= 0) {
        cout << "[ERROR] MASTER_PORT is wrong!" << endl;
        exit(-1);
    }
    
    master_addr.sin_family = AF_INET;
    master_addr.sin_addr = master_ip;
    master_addr.sin_port = htons(master_port);

    // listen for working
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("[ERROR] Create socket failed");
        exit(-1);
    }

    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = inet_addr(LISTEN_ADDR);
    listen_addr.sin_port = htons(listen_port);
    if (bind(listen_fd, (const struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
        perror("[ERROR] Bind socket failed");
        exit(-1);
    }

    if (listen(listen_fd, LISTEN_NUM) < 0) {
        perror("[ERROR] Listen socket failed");
        exit(-1);
    }

    if (pthread_create(&hb_thread, NULL, heart_beat, &master_addr) < 0) {
        perror("[ERROR] Create heartbeat thread failed");
        exit(-1);
    }
    if (thread_pool_init(work_func, POOL_SIZE, QUEUE_SIZE) < 0) {
        perror("[ERROR] Create working thread pool failed");
        exit(-1);
    }

    cout << "[INFO] listening on " << LISTEN_ADDR << ":" << listen_port << endl;

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len;
        int *client_fd = (int *)malloc(sizeof(int));
        pthread_t listen_thread;
       
        *client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("[ERROR] Accept failed");
            exit(-1);
        }
        
        //cout << "[INFO] Accept connectiong from " << inet_ntoa(client_addr.sin_addr) << endl;
        thread_pool_put(client_fd);
    }
}

void *work_func(void *arg)
{
    int client_fd = *(int *)arg;
    char recvbuf[RECV_BUF_SIZE] = {0};
    int recvlen;
    size_t size;
    char *buf;

    ClientTask ct;
    TaskResult tr;
    double ret_double;
    int64_t ret_int64;

    recvlen = recv(client_fd, recvbuf, RECV_BUF_SIZE, 0);
    if (recvlen < 0) {
        perror("[Warning] Recv failed");
        free(arg);
        close(client_fd);
        return NULL;
    }
    if (recvlen == 0) {
        free(arg);
        close(client_fd);
        return NULL;
    }

    memcpy(&size, recvbuf, 4);
    ct.ParseFromArray(recvbuf + 4, size);
    try {
        switch(ct.op()) {
            case 0:
                if (ct.num1_case() == 2)
                    if (ct.num2_case() == 4)
                        ret_int64 = ct.int_num1() + ct.int_num2();
                    else
                        ret_double = ct.int_num1() + ct.double_num2();
                else
                    if (ct.num2_case() == 4)
                        ret_double = ct.double_num1() + ct.int_num2();
                    else
                        ret_double = ct.double_num1() + ct.double_num2();
                break;
            case 1:
                if (ct.num1_case() == 2)
                    if (ct.num2_case() == 4)
                        ret_int64 = ct.int_num1() - ct.int_num2();
                    else
                        ret_double = ct.int_num1() - ct.double_num2();
                else
                    if (ct.num2_case() == 4)
                        ret_double = ct.double_num1() - ct.int_num2();
                    else
                        ret_double = ct.double_num1() - ct.double_num2();
                break;
            case 2:
                if (ct.num1_case() == 2)
                    if (ct.num2_case() == 4)
                        ret_int64 = ct.int_num1() * ct.int_num2();
                    else
                        ret_double = ct.int_num1() * ct.double_num2();
                else
                    if (ct.num2_case() == 4)
                        ret_double = ct.double_num1() * ct.int_num2();
                    else
                        ret_double = ct.double_num1() * ct.double_num2();
                break;
            case 3:
                if (ct.num2_case() == 4) {
                    if (ct.int_num2() == 0)
                        throw "Division by zero!";
                } else
                    if (ct.double_num2() == 0)
                        throw "Division by zero!";

                if (ct.num1_case() == 2)
                    if (ct.num2_case() == 4)
                        ret_int64 = ct.int_num1() / ct.int_num2();
                    else
                        ret_double = ct.int_num1() / ct.double_num2();
                else
                    if (ct.num2_case() == 4)
                        ret_double = ct.double_num1() / ct.int_num2();
                    else
                        ret_double = ct.double_num1() / ct.double_num2();
                break;
            default:
                break;
        }
        tr.set_is_error(false);
        if (ct.num1_case() == 2 && ct.num2_case() == 4)
            tr.set_int_result(ret_int64);
        else
            tr.set_double_result(ret_double);

        size = tr.ByteSizeLong();
        buf = (char *)malloc(size + 4);
        memcpy(buf, &size, 4);
        tr.SerializeToArray(buf + 4, size);
        send(client_fd, buf, size + 4, 0);
    } catch (const char *msg) {
        tr.set_is_error(true);
        tr.set_err_msg(string(msg));

        size = tr.ByteSizeLong();
        buf = (char *)malloc(size + 4);
        memcpy(buf, &size, 4);
        tr.SerializeToArray(buf + 4, size);
        send(client_fd, buf, size + 4, 0);
    }

    free(buf);
    free(arg);
    close(client_fd);
}

void *heart_beat(void *arg)
{
    struct sockaddr_in *master_addr = (struct sockaddr_in *)arg;
    int hb_fd;
    size_t size;
    char *buf;

    WorkerInfo wi;
    
    wi.set_port(listen_port);
    size = wi.ByteSizeLong();
    buf = (char *)malloc(size + 4);
    memcpy(buf, &size, 4);
    wi.SerializeToArray(buf + 4, size);


    for (; 1; sleep(HB_INTERVAL)) {
        hb_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (hb_fd < 0) {
            perror("[WARNING] Create heart beat socket failed");
            continue;
        }
        if (connect(hb_fd, (const struct sockaddr *)master_addr, sizeof(struct sockaddr_in)) < 0) {
            perror("[WARNING] Connect to master failed");
            close(hb_fd);
            continue;
        }
        send(hb_fd, buf, size + 4, 0);
        close(hb_fd);
    }
}
