#include <stdio.h>
#include <string.h>
#include "filemq.h"

int main(){
    char buff[1024] = {0};

    FileMQ *filemq = filemq_init("/home/lyg001/Documents/FileMessageQueue/mq",100);

    for(;;){
        memset(buff,0,sizeof(buff));
        read_filemq(filemq,buff,sizeof(buff),-1);
        printf("%s\n",buff);
    }

    return 0;
}
