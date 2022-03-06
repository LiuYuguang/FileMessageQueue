#include <stdio.h>
#include "filemq.h"

int main(){
    FileMQ *filemq = filemq_init("/home/lyg001/Documents/FileMessageQueue/mq",1024);

    return 0;
}