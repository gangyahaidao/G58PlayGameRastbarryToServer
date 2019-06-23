#ifndef COMMON_H
#define COMMON_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <vector>
#include <iostream>
#include <algorithm>
#include <fstream>

#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/sem.h>

/**
 * 返回时间秒
 * */
long sysUsecTime(void){
    struct timeval tv;
    struct timezone tz;
    //struct tm *t;
    gettimeofday(&tv, &tz);
    //printf("tv_sec:%ld\n",tv.tv_sec);
    return tv.tv_sec;
}

//返回当前的毫秒数
struct timeval tv;
long long getCurrentMsecTime(){    
    gettimeofday(&tv, NULL);
    return (tv.tv_sec*1000+tv.tv_usec/1000);
}

#endif