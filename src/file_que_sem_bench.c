#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "filemq.h"

#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

uint64_t _localtime(){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int bench_latency(int count){
    printf("测试触发延迟, 次数%d\r",count);
    pid_t pid = fork();
    if(pid < 0){
        fprintf(stderr,"fork fail %d %s\n",errno,strerror(errno));
        return -1;
    }
    if(pid == 0){
        int tmp_len,i;
        uint64_t past,now,diff;
        uint64_t total_count=0,total_diff=0;
        uint64_t min_diff=-1,max_diff=0,average_diff=0;

        for(i=0;i<count;i++){
            tmp_len = read_file_que_timedwait_sem("./tmp",&past,sizeof(past),-1);
            if(tmp_len != sizeof(past))
                continue;
            now = _localtime();
            //printf("[%d] past[%lu] now[%lu] diff[%lu]us\n",tmp_len,past,now,now-past);
            diff = now - past;
            if(diff < min_diff){
                min_diff = diff;
            }
            if(diff > max_diff){
                max_diff = diff;
            }
            total_count++;
            total_diff += diff;
        }

        average_diff = total_diff / total_count;
        printf("测试触发延迟, 次数%d, total_count[%lu],total_diff[%lu],min_diff[%lu],max_diff[%lu],average_diff[%lu]\n",
            count,
            total_count,total_diff,min_diff,max_diff,average_diff);
        exit(0);
    }else{
        uint64_t now;
        int i;
        //past = _localtime();
        for(i=0;i<count;i++){
            now = _localtime();
            write_file_que_timedwait_sem("./tmp",&now,sizeof(now),5);
            usleep(1000);
        }

        int pid_status;
        waitpid(pid,&pid_status,0);
    }
    return 0;
}

int bench_write(int count){
    printf("测试只写耗时, 次数%d\r",count);
    uint64_t past,now;
    int i;
    past = _localtime();
    for(i=0;i<count;i++){
        write_file_que_timedwait_sem("./tmp",&now,sizeof(now),5);
    }
    now = _localtime();
    printf("测试只写耗时, 次数%d, 耗时%lu\n", count,now-past);
    return 0;
}

int bench_read(int count){
    printf("测试只读耗时, 次数%d\r",count);
    uint64_t past,now;
    int i;
    past = _localtime();
    for(i=0;i<count;i++){
        read_file_que_timedwait_sem("./tmp",&now,sizeof(now),5);
    }
    now = _localtime();
    printf("测试只读耗时, 次数%d, 耗时%lu\n",count,now-past);
    return 0;
}

int main(int argc,char **argv){
    int count = 10000;
    if(argc > 1){
        count = atoi(argv[1]);
    }

    bench_latency(count);

    bench_write(count);

    bench_read(count);

    return 0;
}
