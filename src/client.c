#include <stdio.h>
#include <string.h>
#include "filemq.h"

int main(){
    char buff[1024] = {0};
    FileMQ *filemq = filemq_init("/home/lyg001/Documents/FileMessageQueue/mq",100);

    while(scanf("%1023[^\n]%*c",buff) != EOF){
        printf("input %s\n",buff);
        write_filemq(filemq,buff,strlen(buff),-1);
        printf("write done %s\n",buff);
    }

    return 0;
}
