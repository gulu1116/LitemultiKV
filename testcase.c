#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>


#define MAX_MSG_LENGTH 1024
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


int send_msg(int connfd, char *msg, int length) {

    int res = send(connfd, msg, length, 0);
    if (res < 0) {
        perror("send");
        exit(1);
    }
    return res;
}

int recv_msg(int connfd, char *msg, int length) {

    int res = recv(connfd, msg, length, 0);
    if (res < 0) {
        perror("send");
        exit(1);
    }
    return res;
}

void testcase(int connfd, char *msg, char *pattern, char *casename) {

    if (!msg || !pattern || !casename) return;

    send_msg(connfd, msg, strlen(msg));

    char result[MAX_MSG_LENGTH] = {0};
    recv_msg(connfd, result, MAX_MSG_LENGTH);

    if (strcmp(result, pattern) == 0) {
        //  printf("testcase %s passed\n", casename);
    } else {
        // printf("testcase %s failed, expected: %s, got: %s\n", casename, pattern, result);
        exit(1);
    }

}


int connect_tcpserver(const char *ip, unsigned short port) {

    int connfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    if (0 != connect(connfd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in))) {

        perror("connect");
        return -1;
    }

    return connfd;
}


void array_testcase(int connfd) {

    testcase(connfd, "SET Student GuLu", "OK\r\n", "SET-Student-GuLu");
    testcase(connfd, "GET Student", "GuLu\r\n", "GET-Student");
    testcase(connfd, "MOD Student Lighting", "OK\r\n", "MOD-Student-Lighting");
    testcase(connfd, "GET Student", "Lighting\r\n", "GET-Student");
    testcase(connfd, "EXIST Student", "EXIST\r\n", "EXIST-Student");
    testcase(connfd, "DEL Student", "OK\r\n", "DEL-Student");
    testcase(connfd, "GET Student", "NO EXIST\r\n", "GET-Student");
    testcase(connfd, "MOD Student Lian", "NO EXIST\r\n", "MOD-Student-Lian");
    testcase(connfd, "EXIST Student", "NO EXIST\r\n", "EXIST-Student");

}

void array_testcase_1w(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i ++) {

        testcase(connfd, "SET Student GuLu", "OK\r\n", "SET-Student-GuLu");
        testcase(connfd, "GET Student", "GuLu\r\n", "GET-Student");
        testcase(connfd, "MOD Student Lighting", "OK\r\n", "MOD-Student-Lighting");
        testcase(connfd, "GET Student", "Lighting\r\n", "GET-Student");
        testcase(connfd, "EXIST Student", "EXIST\r\n", "EXIST-Student");
        testcase(connfd, "DEL Student", "OK\r\n", "DEL-Student");
        testcase(connfd, "GET Student", "NO EXIST\r\n", "GET-Student");
        testcase(connfd, "MOD Student Lian", "NO EXIST\r\n", "MOD-Student-Lian");
        testcase(connfd, "EXIST Student", "NO EXIST\r\n", "EXIST-Student");
    }

    struct timeval tv_end;
	gettimeofday(&tv_end, NULL);

	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

	printf("array testcase_1w --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);

}

void array_testcase_1w_0(int connfd) {
    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    // 插入阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "SET Student%d GuLu%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "SET-Student-GuLu");
    }

    // 查询阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "GET Student%d", i);
        char result[128] = {0};
        snprintf(result, sizeof(result), "GuLu%d\r\n", i);
        testcase(connfd, cmd, result, "GET-Student");
    }

    // 修改阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "MOD Student%d Lighting%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "MOD-Student-Lighting");
    }

    // 再次查询阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "GET Student%d", i);
        char result[128] = {0};
        snprintf(result, sizeof(result), "Lighting%d\r\n", i);
        testcase(connfd, cmd, result, "GET-Student");
    }

    // 存在性检查阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "EXIST Student%d", i);
        testcase(connfd, cmd, "EXIST\r\n", "EXIST-Student");
    }

    // 删除阶段
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "DEL Student%d", i);
        testcase(connfd, cmd, "OK\r\n", "DEL-Student");
    }

    // 查询删除后的数据
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "GET Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "GET-Student");
    }

    // 修改不存在的数据
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "MOD Student%d Lian%d", i, i);
        testcase(connfd, cmd, "NO EXIST\r\n", "MOD-Student");
    }

    // 最终存在性检查（确认已删除）
    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, sizeof(cmd), "EXIST Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "EXIST-Student");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = TIME_SUB_MS(tv_end, tv_begin);
    printf("array testcase_1w_0 --> time_used: %d ms, qps: %d\n",
           time_used, 90000 * 1000 / time_used);
}


void rbtree_testcase(int connfd) {

    testcase(connfd, "RSET Student GuLu", "OK\r\n", "RSET-Student-GuLu");
    testcase(connfd, "RGET Student", "GuLu\r\n", "RGET-Student");
    testcase(connfd, "RMOD Student Lighting", "OK\r\n", "RMOD-Student-Lighting");
    testcase(connfd, "RGET Student", "Lighting\r\n", "RGET-Student");
    testcase(connfd, "REXIST Student", "EXIST\r\n", "REXIST-Student");
    testcase(connfd, "RDEL Student", "OK\r\n", "RDEL-Student");
    testcase(connfd, "RGET Student", "NO EXIST\r\n", "RGET-Student");
    testcase(connfd, "RMOD Student Lian", "NO EXIST\r\n", "RMOD-Student-Lian");
    testcase(connfd, "REXIST Student", "NO EXIST\r\n", "REXIST-Student");

}

void rbtree_testcase_1w(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i ++) {

        testcase(connfd, "RSET Student GuLu", "OK\r\n", "RSET-Student-GuLu");
        testcase(connfd, "RGET Student", "GuLu\r\n", "RGET-Student");
        testcase(connfd, "RMOD Student Lighting", "OK\r\n", "RMOD-Student-Lighting");
        testcase(connfd, "RGET Student", "Lighting\r\n", "RGET-Student");
        testcase(connfd, "REXIST Student", "EXIST\r\n", "REXIST-Student");
        testcase(connfd, "RDEL Student", "OK\r\n", "RDEL-Student");
        testcase(connfd, "RGET Student", "NO EXIST\r\n", "RGET-Student");
        testcase(connfd, "RMOD Student Lian", "NO EXIST\r\n", "RMOD-Student-Lian");
        testcase(connfd, "REXIST Student", "NO EXIST\r\n", "REXIST-Student");
    }

    struct timeval tv_end;
	gettimeofday(&tv_end, NULL);

	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

	printf("rbtree testcase_1w --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);

}



void rbtree_testcase_1w_0(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RSET Student%d GuLu%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "RSET-Student-GuLu");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RGET Student%d", i);

        char result[128] = {0};
        snprintf(result, 128, "GuLu%d\r\n", i);
        testcase(connfd, cmd, result, "RGET-Student");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RMOD Student%d Lighting%d", i, i);
        testcase(connfd, cmd, "OK\r\n",  "RMOD-Student-Lighting");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RGET Student%d", i);

        char result[128] = {0};
        snprintf(result, 128, "Lighting%d\r\n", i);
        testcase(connfd, cmd, result, "RGET-Student");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "REXIST Student%d", i);
        testcase(connfd, cmd, "EXIST\r\n", "REXIST-Student");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RDEL Student%d", i);
        testcase(connfd, cmd, "OK\r\n", "RDEL-Student");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RGET Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "RGET-Student");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "RMOD Student%d Lian%d", i, i);
        testcase(connfd, cmd, "NO EXIST\r\n",  "RMOD-Student-Lian");
    }

    for (i = 0; i < count; i ++) {

        char cmd[128] = {0};
        snprintf(cmd, 128, "REXIST Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "REXIST-Student");
    }

    struct timeval tv_end;
	gettimeofday(&tv_end, NULL);

	int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms

	printf("rbtree testcase_1w_0 --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);

}


void hash_testcase(int connfd) {

    testcase(connfd, "HSET Student GuLu", "OK\r\n", "HSET-Student-GuLu");
    testcase(connfd, "HGET Student", "GuLu\r\n", "HGET-Student");
    testcase(connfd, "HMOD Student Lighting", "OK\r\n", "HMOD-Student-Lighting");
    testcase(connfd, "HGET Student", "Lighting\r\n", "HGET-Student");
    testcase(connfd, "HEXIST Student", "EXIST\r\n", "HEXIST-Student");
    testcase(connfd, "HDEL Student", "OK\r\n", "HDEL-Student");
    testcase(connfd, "HGET Student", "NO EXIST\r\n", "HGET-Student");
    testcase(connfd, "HMOD Student Lian", "NO EXIST\r\n", "HMOD-Student-Lian");
    testcase(connfd, "HEXIST Student", "NO EXIST\r\n", "HEXIST-Student");

}

void hash_testcase_1w(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i++) {
        testcase(connfd, "HSET Student GuLu", "OK\r\n", "HSET-Student-GuLu");
        testcase(connfd, "HGET Student", "GuLu\r\n", "HGET-Student");
        testcase(connfd, "HMOD Student Lighting", "OK\r\n", "HMOD-Student-Lighting");
        testcase(connfd, "HGET Student", "Lighting\r\n", "HGET-Student");
        testcase(connfd, "HEXIST Student", "EXIST\r\n", "HEXIST-Student");
        testcase(connfd, "HDEL Student", "OK\r\n", "HDEL-Student");
        testcase(connfd, "HGET Student", "NO EXIST\r\n", "HGET-Student");
        testcase(connfd, "HMOD Student Lian", "NO EXIST\r\n", "HMOD-Student-Lian");
        testcase(connfd, "HEXIST Student", "NO EXIST\r\n", "HEXIST-Student");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
    printf("hash testcase_1w --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
}

void hash_testcase_1w_0(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HSET Student%d GuLu%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "HSET-Student-GuLu");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HGET Student%d", i);

        char result[128] = {0};
        snprintf(result, 128, "GuLu%d\r\n", i);
        testcase(connfd, cmd, result, "HGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HMOD Student%d Lighting%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "HMOD-Student-Lighting");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HGET Student%d", i);

        char result[128] = {0};
        snprintf(result, 128, "Lighting%d\r\n", i);
        testcase(connfd, cmd, result, "HGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HEXIST Student%d", i);
        testcase(connfd, cmd, "EXIST\r\n", "HEXIST-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HDEL Student%d", i);
        testcase(connfd, cmd, "OK\r\n", "HDEL-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HGET Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "HGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HMOD Student%d Lian%d", i, i);
        testcase(connfd, cmd, "NO EXIST\r\n", "HMOD-Student-Lian");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "HEXIST Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "HEXIST-Student");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
    printf("hash testcase_1w_0 --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
}

void skiplist_testcase(int connfd) {
    testcase(connfd, "SSET Student GuLu", "OK\r\n", "SSET-Student-GuLu");
    testcase(connfd, "SGET Student", "GuLu\r\n", "SGET-Student");
    testcase(connfd, "SMOD Student Lighting", "OK\r\n", "SMOD-Student-Lighting");
    testcase(connfd, "SGET Student", "Lighting\r\n", "SGET-Student");
    testcase(connfd, "SEXIST Student", "EXIST\r\n", "SEXIST-Student");
    testcase(connfd, "SDEL Student", "OK\r\n", "SDEL-Student");
    testcase(connfd, "SGET Student", "NO EXIST\r\n", "SGET-Student");
    testcase(connfd, "SMOD Student Lian", "NO EXIST\r\n", "SMOD-Student-Lian");
    testcase(connfd, "SEXIST Student", "NO EXIST\r\n", "SEXIST-Student");
}

void skiplist_testcase_1w(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i++) {
        testcase(connfd, "SSET Student GuLu", "OK\r\n", "SSET-Student-GuLu");
        testcase(connfd, "SGET Student", "GuLu\r\n", "SGET-Student");
        testcase(connfd, "SMOD Student Lighting", "OK\r\n", "SMOD-Student-Lighting");
        testcase(connfd, "SGET Student", "Lighting\r\n", "SGET-Student");
        testcase(connfd, "SEXIST Student", "EXIST\r\n", "SEXIST-Student");
        testcase(connfd, "SDEL Student", "OK\r\n", "SDEL-Student");
        testcase(connfd, "SGET Student", "NO EXIST\r\n", "SGET-Student");
        testcase(connfd, "SMOD Student Lian", "NO EXIST\r\n", "SMOD-Student-Lian");
        testcase(connfd, "SEXIST Student", "NO EXIST\r\n", "SEXIST-Student");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
    printf("skiplist testcase_1w --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
}

void skiplist_testcase_1w_0(int connfd) {

    int count = 10000;
    int i = 0;

    struct timeval tv_begin;
    gettimeofday(&tv_begin, NULL);

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SSET Student%d GuLu%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "SSET-Student-GuLu");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SGET Student%d", i);
        char result[128] = {0};
        snprintf(result, 128, "GuLu%d\r\n", i);
        testcase(connfd, cmd, result, "SGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SMOD Student%d Lighting%d", i, i);
        testcase(connfd, cmd, "OK\r\n", "SMOD-Student-Lighting");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SGET Student%d", i);
        char result[128] = {0};
        snprintf(result, 128, "Lighting%d\r\n", i);
        testcase(connfd, cmd, result, "SGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SEXIST Student%d", i);
        testcase(connfd, cmd, "EXIST\r\n", "SEXIST-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SDEL Student%d", i);
        testcase(connfd, cmd, "OK\r\n", "SDEL-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SGET Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "SGET-Student");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SMOD Student%d Lian%d", i, i);
        testcase(connfd, cmd, "NO EXIST\r\n", "SMOD-Student-Lian");
    }

    for (i = 0; i < count; i++) {
        char cmd[128] = {0};
        snprintf(cmd, 128, "SEXIST Student%d", i);
        testcase(connfd, cmd, "NO EXIST\r\n", "SEXIST-Student");
    }

    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    int time_used = TIME_SUB_MS(tv_end, tv_begin); // ms
    printf("skiplist testcase_1w_0 --> time_used: %d, qps: %d\n", time_used, 90000 * 1000 / time_used);
}



// testcase 192.168.127.141 8000
int main(int argc, char *argv[]) {

    if(argc != 3) {
        printf("argc error\n");
        return -1;
    }

    //getopt(argc, argv)

    char *ip = argv[1];
    int port = atoi(argv[2]);
    //int mode = atoi(argv[3]);

    int connfd = connect_tcpserver(ip, port);

    // fork();
    // fork();
    // fork();

    // if (mode == 0) {
    //     rbtree_testcase_1w(connfd);
    // } else if (mode == 1) {
    //     rbtree_testcase_1w_0(connfd);
    // } else if (mode == 2) {
    //     array_testcase_1w(connfd);
    // } else if (mode == 3) {
    //     hash_testcase(connfd);
    // }


    rbtree_testcase_1w(connfd);
    rbtree_testcase_1w_0(connfd);
    array_testcase_1w(connfd);
    array_testcase_1w_0(connfd);
    hash_testcase_1w(connfd);
    hash_testcase_1w_0(connfd);
    skiplist_testcase_1w(connfd);
    hash_testcase(connfd);
    skiplist_testcase(connfd);
    skiplist_testcase_1w_0(connfd);

    

    return 0;
}