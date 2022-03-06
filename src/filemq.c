#define _GNU_SOURCE
#include "filemq.h"
#include <string.h>                // for memcpy
#include <stdlib.h>                // for malloc free
#include <errno.h>                 // for errno E*
#include <fcntl.h>                 // for fcntl
#include <unistd.h>                // for ftruncate
#include <sys/stat.h>              // for fstat
#include <sys/time.h>              // for gettimeofday
#include <sys/sem.h>               // for semget semop semctl
#include <stdio.h>
#include <linux/limits.h>          // for MAX_PATH
#include <libgen.h>                // for dirname
#include <sys/mman.h>              // for mmap

typedef struct{
	size_t head_index;
	size_t tail_index;
    size_t capacity;
    unsigned char data[0];
}FileMQHEAD;

typedef struct _FileMQ{
    int sem_id;
    FileMQHEAD *head;
}FileMQ;

union semun
{
	int val;                /* for SETVAL */
	struct semid_ds *buf;   /* for IPC_STAT and IPC_SET */
	unsigned short *array;  /* for GETALL and SETALL */
};

#define READABLE           (12)
#define UNREADABLE         (10)
#define WRITEABLE          (12)
#define UNWRITEABLE        (10)

#define READLOCK           (11)
#define WRITELOCK          (11)
#define LOCK               (9)

int makedirs(const char *path){
    //参数错误
    if(path == NULL){
        errno = EINVAL;
        return -1;
    }

    struct stat file_stat;
    //路径存在
    if(stat(path,&file_stat) == 0 && S_ISDIR(file_stat.st_mode)){
        return 0;
    }

	char path_tmp[PATH_MAX+1]={0},path_total[PATH_MAX+1]={0};
    char *index;

    if(strlen(path) > PATH_MAX){
        errno = E2BIG;
        return -1;
    }
    strcpy(path_tmp,path);

    if(path_tmp[0] == '/'){
        strcat(path_total,"/");
    }
    
	for(index = strtok(path_tmp,"/");index!=NULL;index=strtok(NULL,"/")){
        strcat(path_total,index);
        strcat(path_total,"/");
        if(stat(path_total,&file_stat) == 0){
            if(S_ISDIR(file_stat.st_mode)){
                //路径存在
                continue;
            }
            else{
                //其他类型文件
                return -1;
            }
        }

        if(mkdir(path_total,0775) == -1){
            return -1;
        }
    }
    return 0;
}

FileMQ *filemq_init(const char* filename,size_t capacity){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};
	int fd = -1,sem_id;
	key_t sem_key;
	struct stat file_stat;
	FileMQHEAD *head=NULL;

	if(filename == NULL || strlen(filename) > PATH_MAX){
		errno = EINVAL;
		return NULL;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		char file_path[PATH_MAX+1]={0};
		strcpy(file_path,filename);
		//创建路径
        if(makedirs(dirname(file_path)) == -1){
            return NULL;
        }
		fd = open(filename,O_RDWR|O_CREAT,0664);
		if(fd == -1){
			return NULL;
		}

	}
	fstat(fd,&file_stat);
	if(file_stat.st_size <= sizeof(FileMQHEAD)){
		if(ftruncate(fd,sizeof(FileMQHEAD) + capacity) == -1){
			close(fd);
			return NULL;
		}
		file_stat.st_size = sizeof(FileMQHEAD) + capacity;
	}

	if((head = mmap(NULL,file_stat.st_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0)) == MAP_FAILED){
		close(fd);
		return NULL;
	}

	if(file_stat.st_size != sizeof(FileMQHEAD) + head->capacity){
		head->head_index = head->tail_index = 0;
		head->capacity = file_stat.st_size - sizeof(FileMQHEAD);
	}
	
	sem_key = ftok(filename,1);
	sem_id = semget(sem_key,0,0664);
	if(sem_id == -1){
		// 信号量不存在, 创建信号量
		if(errno == ENOENT){
			//锁文件
			fcntl(fd,F_SETLKW,&lock);
			sem_id = semget(sem_key,0,0664);
			if(sem_id == -1 && errno == ENOENT){
				sem_id = semget(sem_key,2,0664|IPC_CREAT);
				if(sem_id != -1){
					// 设置初值, 默认为可读可写
					unsigned short array[] = {READABLE,WRITEABLE};
					union semun sem_union = {.array = array};
					semctl(sem_id,0,SETALL,sem_union);
				}
			}
			//解锁文件
			fcntl(fd,F_SETLKW,&unlock);
		}
	}

	if(sem_id  == -1){
		close(fd);
		munmap(head,file_stat.st_size);
		return NULL;
	}

	FileMQ *filemq = malloc(sizeof(FileMQ));
	filemq->sem_id = sem_id;
	filemq->head = head;
	close(fd);

	return filemq;
}

ssize_t write_filemq(FileMQ* filemq,void* pdata,size_t len,int timeout){
	FileMQHEAD *head = filemq->head;
	int retu;
	size_t new_head_index;

	if(sizeof(len) + len > head->capacity){
		errno = EINVAL;
		return -1;
	}

	start:

	if(timeout < 0){
		struct sembuf sem_b[2] = {
			{0,-LOCK,SEM_UNDO},
			{1,-WRITELOCK,SEM_UNDO},
		};

		retu = semop(filemq->sem_id,sem_b,2);
	}else if(timeout == 0){
		struct sembuf sem_b[2] = {
			{0,-LOCK,SEM_UNDO|IPC_NOWAIT},
			{1,-WRITELOCK,SEM_UNDO|IPC_NOWAIT},
		};
		retu = semop(filemq->sem_id,sem_b,2);
	}else{
		struct timespec ts;
		ts.tv_sec = timeout/1000;
		ts.tv_nsec = (timeout%1000)*1000000;
		struct sembuf sem_b[2] = {
			{0,-LOCK,SEM_UNDO},
			{1,-WRITELOCK,SEM_UNDO},
		};
		retu = semtimedop(filemq->sem_id,sem_b,2,&ts);
	}
	if(retu == -1){
		return retu;
	}
	
	if(sizeof(len) + len > head->capacity - ((head->head_index + head->capacity - head->tail_index)%head->capacity)){
		// 容量不足, 不可写
		unsigned short array[] = {READABLE,UNWRITEABLE};
		union semun sem_union = {.array = array};
		semctl(filemq->sem_id,0,SETALL,sem_union);
		goto start;
	}
	
	new_head_index = head->head_index;

	if(new_head_index + sizeof(len) > head->capacity){
		memcpy(&head->data[new_head_index],&len,head->capacity-new_head_index);
		memcpy(&head->data[0],(unsigned char*)&len + head->capacity-new_head_index,sizeof(len) - (head->capacity-new_head_index));
		new_head_index = (new_head_index+sizeof(len)) % head->capacity;
	}else{
		memcpy(&head->data[new_head_index],(unsigned char*)&len,sizeof(len));
		new_head_index = (new_head_index+sizeof(len)) % head->capacity;
	}

	if(new_head_index + len > head->capacity){
		memcpy(&head->data[new_head_index],pdata,head->capacity-new_head_index);
		memcpy(&head->data[0],(unsigned char*)pdata + head->capacity-new_head_index,len - (head->capacity-new_head_index));
		new_head_index = (new_head_index+len) % head->capacity;
	}else{
		memcpy(&head->data[new_head_index],pdata,len);
		new_head_index = (new_head_index+len) % head->capacity;
	}
	head->head_index = new_head_index;

	unsigned short array[] = {READABLE,WRITEABLE};
	union semun sem_union = {.array = array};
	semctl(filemq->sem_id,0,SETALL,sem_union);
	return len;
}

ssize_t read_filemq(FileMQ* filemq,void* pdata,size_t size,int timeout){
	FileMQHEAD *head = filemq->head;
	size_t len=0,new_tail_index;
	int retu;

	start:
	if(timeout < 0){
		struct sembuf sem_b[2] = {
			{0,-READLOCK,SEM_UNDO},
			{1,-LOCK,SEM_UNDO},
		};//p操纵
		retu = semop(filemq->sem_id,sem_b,2);
	}else if(timeout == 0){
		struct sembuf sem_b[2] = {
			{0,-READLOCK,SEM_UNDO|IPC_NOWAIT},
			{1,-LOCK,SEM_UNDO|IPC_NOWAIT},
		};//p操纵
		retu = semop(filemq->sem_id,sem_b,2);
	}else{
		struct timespec ts;
		ts.tv_sec = timeout/1000;
		ts.tv_nsec = (timeout%1000)*1000000;
		struct sembuf sem_b[2] = {
			{0,-READLOCK,SEM_UNDO},
			{1,-LOCK,SEM_UNDO},
		};//p操纵
		retu = semtimedop(filemq->sem_id,sem_b,2,&ts);
	}

	if(retu == -1){
		return retu;
	}

	if(head->head_index == head->tail_index){
		// 不可读
		unsigned short array[] = {UNREADABLE,WRITEABLE};
		union semun sem_union = {.array = array};
		semctl(filemq->sem_id,0,SETALL,sem_union);
		goto start;
	}

	int i;
	for(new_tail_index=head->tail_index,i=0;i<sizeof(len);new_tail_index=(new_tail_index+1)%(head->capacity),i++){
		((unsigned char*)&len)[i] = head->data[new_tail_index];
	}

	if(len > size){
		// 数据太长
		struct sembuf sem_b[2] = {
			{0,READLOCK,SEM_UNDO},
			{1,LOCK,SEM_UNDO},
		};//p操纵
		semop(filemq->sem_id,sem_b,2);
		errno = E2BIG;
		return -1;
	}

	if(new_tail_index + len > head->capacity){
		memcpy(&((unsigned char*)pdata)[0], &head->data[new_tail_index],head->capacity - new_tail_index);
		memcpy(&((unsigned char*)pdata)[head->capacity - new_tail_index],&head->data[0],len - (head->capacity - new_tail_index));
		new_tail_index = (new_tail_index + len) % head->capacity;
	}else{
		memcpy(&((unsigned char*)pdata)[0], &head->data[new_tail_index],len);
		new_tail_index = (new_tail_index + len) % head->capacity;
	}
	head->tail_index = new_tail_index;

	if(head->head_index == head->tail_index){
		unsigned short array[] = {UNREADABLE,WRITEABLE};
		union semun sem_union = {.array = array};
		semctl(filemq->sem_id,0,SETALL,sem_union);
	}else{
		struct sembuf sem_b[2] = {
			{0,READLOCK,SEM_UNDO},
			{1,LOCK,SEM_UNDO},
		};//p操纵
		semop(filemq->sem_id,sem_b,2);
	}

	return len;
}