#define _GNU_SOURCE
#include "filemq.h"
#include <string.h>                //for memcpy
#include <stdlib.h>                //for malloc free
#include <signal.h>                //for signal sigset_t sigfillset pthread_sigmask SIG*
#include <errno.h>                 //for errno E*
#include <unistd.h>                //for alarm
#include <sys/inotify.h>           //for inotify
#include <fcntl.h>                 //for fcntl
#include <unistd.h>                //for ftruncate
#include <sys/stat.h>              //for fstat
#include <sys/time.h>              //for gettimeofday
#include <sys/sem.h>               //for semget semop semctl
#include <stdio.h>

#define FILE_QUE_HEAD_SIZE 4096
#define FILE_QUE_BODY_SIZE 4096

typedef struct{
	size_t head_index;
	size_t tail_index;
}file_que_head;

typedef struct{
	size_t length;
	unsigned char data[0];
}file_que_body;

static void _localtime(uint64_t *t){
    struct timeval tv;
    gettimeofday(&tv,NULL);
    *t = tv.tv_sec*1000000 + tv.tv_usec;
}

///////////////////////////////以下是inotify////////////////////////////////////////////////////////////
ssize_t write_file_que_timedwait_inotify(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd;

	if(filename == NULL){
		return -1;
	}

	//pdata太长
	if(len > FILE_QUE_BODY_SIZE-sizeof(file_que_body)){
		return -1;
	}
	
	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1)
		return -1;

	//锁文件
	fcntl(fd,F_SETLKW,&lock);
	//文件长度
	fstat(fd,&filestat);
	//读头
	if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
		lseek(fd,0,SEEK_SET);
		read(fd,head_buf,FILE_QUE_HEAD_SIZE);
	}
	//脏文件,清空
	if(filestat.st_size<FILE_QUE_HEAD_SIZE 
		||que_head->head_index<que_head->tail_index 
		|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
	){
		que_head->head_index = que_head->tail_index = 0;
		//ftruncate(fd,0);
	}
	//追加数据
	que_body->length = len;
	memcpy(que_body->data,pdata,len);
	retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE,SEEK_SET);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = write(fd,body_buf,FILE_QUE_BODY_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	//更新头
	que_head->head_index++;
	lseek(fd,0,SEEK_SET);
	retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = len;

	finish:
	//解锁
	fcntl(fd,F_SETLKW,&unlock);
	close(fd);
	return retu;
}

ssize_t read_file_que_timedwait_inotify(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,inotify_fd,inotify_wd;
	uint64_t now,expire;

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}
	//创建inotify
	inotify_fd = inotify_init();

	_localtime(&now);
	
	if(timeout < 0){
		expire = -1;
	}else{
		expire = now + timeout * 1000000;
	}
	
	while(now <= expire){
		memset(head_buf,0,sizeof(head_buf));
		//加锁
		fcntl(fd,F_SETLKW,&lock);
		//文件长度
		fstat(fd,&filestat);
		//读头
		if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
			lseek(fd,0,SEEK_SET);
			read(fd,head_buf,FILE_QUE_HEAD_SIZE);
		}
		//脏文件,清空
		if(filestat.st_size<FILE_QUE_HEAD_SIZE 
			||que_head->head_index<que_head->tail_index 
			|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
		){
			que_head->head_index = que_head->tail_index = 0;
			ftruncate(fd,0);
		}
		//有数据
		if(que_head->head_index > que_head->tail_index){
			//先偏移
			retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
			if(retu == -1){
				retu = -1;
				break;
			}
			//有数据
			while(que_head->head_index > que_head->tail_index){
				retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
				if(retu != FILE_QUE_BODY_SIZE){
					retu = -1;
					break;
				}
				if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
					//脏数据, 继续读
					retu = -1;
					que_head->tail_index++;
					continue;
				}
				if(que_body->length > len){
					//pdata不够长
					retu = -1;
					errno = E2BIG;
					break;
				}
				memcpy(pdata,que_body->data,que_body->length);

				que_head->tail_index++;
				if(que_head->head_index == que_head->tail_index){
					//队列为空, 清空文件
					que_head->head_index = que_head->tail_index = 0;
					ftruncate(fd,0);
				}else{
					lseek(fd,0,SEEK_SET);
					retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
					if(retu == -1){
						retu = -1;
						break;
					}
				}
				retu = que_body->length;
				break;
			}
		}
		//解锁
		fcntl(fd,F_SETLKW,&unlock);

		//取到数据或者读数据有某些问题的，可以返回了
		if(retu >= 0 || que_head->head_index > que_head->tail_index){
			break;
		}
		//队列为空
		if(now >= expire){
			retu = -1;
			break;
		}

		//无数据,则用inotify等待文件写入
		struct timeval tv,*tv_p;
		if(timeout<0){
			tv_p = NULL;
		}else{
			tv_p = &tv;
			tv_p->tv_sec = (expire - now) / 1000000;
			tv_p->tv_usec = (expire - now) % 1000000;
		}
		
		inotify_wd = inotify_add_watch(inotify_fd, filename, IN_OPEN|IN_MODIFY);
		//struct inotify_event event;
		
		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(inotify_fd,&rset);
		int n = select(inotify_fd+1,&rset,NULL,NULL,tv_p);

		if(n == 0){
			//timeout
			errno = ETIME;
			retu = -1;
			break;
		}else if(n == -1){
			//signal or other
			retu = -1;
			break;
		}else{
			//继续读文件
		}
		inotify_rm_watch(fd, inotify_wd); 
		_localtime(&now);
	}
	close(fd);
	close(inotify_fd);
	return retu;
}

ssize_t peek_file_que_timedwait_inotify(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,inotify_fd,inotify_wd;
	uint64_t now,expire;

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}
	//创建inotify
	inotify_fd = inotify_init();

	_localtime(&now);
	
	if(timeout < 0){
		expire = -1;
	}else{
		expire = now + timeout * 1000000;
	}
	
	while(now <= expire){
		memset(head_buf,0,sizeof(head_buf));
		//加锁
		fcntl(fd,F_SETLKW,&lock);
		//文件长度
		fstat(fd,&filestat);
		//读头
		if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
			lseek(fd,0,SEEK_SET);
			read(fd,head_buf,FILE_QUE_HEAD_SIZE);
		}
		//脏文件,清空
		if(filestat.st_size<FILE_QUE_HEAD_SIZE 
			||que_head->head_index<que_head->tail_index 
			|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
		){
			que_head->head_index = que_head->tail_index = 0;
			ftruncate(fd,0);
		}
		//有数据
		size_t tail_index_old = que_head->tail_index;
		if(que_head->head_index > que_head->tail_index){
			//先偏移
			retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
			if(retu == -1){
				retu = -1;
				break;
			}
			//有数据
			while(que_head->head_index > que_head->tail_index){
				retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
				if(retu != FILE_QUE_BODY_SIZE){
					retu = -1;
					break;
				}
				if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
					//脏数据, 继续读
					retu = -1;
					que_head->tail_index++;
					continue;
				}
				if(que_body->length > len){
					//pdata不够长
					retu = -1;
					errno = E2BIG;
					break;
				}
				memcpy(pdata,que_body->data,que_body->length);
				if(tail_index_old != que_head->tail_index){
					lseek(fd,0,SEEK_SET);
					retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
					if(retu == -1){
						retu = -1;
						break;
					}
				}
				retu = que_body->length;
				break;
			}
		}
		//解锁
		fcntl(fd,F_SETLKW,&unlock);

		//取到数据或者读数据有某些问题的，可以返回了
		if(retu >= 0 || que_head->head_index > que_head->tail_index){
			break;
		}
		//队列为空
		if(now >= expire){
			retu = -1;
			break;
		}

		//无数据,则用inotify等待文件写入
		struct timeval tv,*tv_p;
		if(expire == -1){
			tv_p = NULL;
		}else{
			tv_p = &tv;
			tv_p->tv_sec = (expire - now) / 1000000;
			tv_p->tv_usec = (expire - now) % 1000000;
		}
		inotify_wd = inotify_add_watch(inotify_fd, filename, IN_OPEN|IN_MODIFY);
		//struct inotify_event event;
		
		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(inotify_fd,&rset);
		int n = select(inotify_fd+1,&rset,NULL,NULL,tv_p);

		if(n == 0){
			//timeout
			errno = ETIME;
			retu = -1;
			break;
		}else if(n == -1){
			//signal
			retu = -1;
			break;
		}else{
			//继续读文件
		}
		inotify_rm_watch(fd, inotify_wd); 
		_localtime(&now);

	}
	close(fd);
	close(inotify_fd);
	return retu;
}
///////////////////////////////以上是inotify////////////////////////////////////////////////////////////

///////////////////////////////以下是信号量////////////////////////////////////////////////////////////

#define    HAVETRANS    30
#define    NOTRANS      20
#define    WRITETRANS   -18
#define    READTRANS    -25

union semun
{
	int val;
	struct semid_ds *buf;
	unsigned short *arry;
};

ssize_t write_file_que_timedwait_sem(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,sem_id;
	key_t sem_key;
	union semun sem_union;
	struct sembuf sem_b = {0,WRITETRANS,SEM_UNDO};//p操纵

	if(filename == NULL){
		return -1;
	}

	//pdata太长
	if(len > FILE_QUE_BODY_SIZE-sizeof(file_que_body)){
		return -1;
	}
	
	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1)
		return -1;

	sem_key = ftok(filename,1);
	sem_id = semget(sem_key,1,0664);
	if(sem_id == -1){
		//信号量不存在, 创建信号量
		if(errno == ENOENT){
			//锁文件
			fcntl(fd,F_SETLKW,&lock);
			sem_id = semget(sem_key,1,0664);
			if(sem_id == -1 && errno == ENOENT){
				sem_id = semget(sem_key,1,0664|IPC_CREAT);
				if(sem_id != -1){
					//设置初值
					sem_union.val = NOTRANS;
					semctl(sem_id,0,SETVAL,sem_union);
				}
			}
			//解锁文件
			fcntl(fd,F_SETLKW,&unlock);
		}
	}
	if(sem_id  == -1){
		close(fd);
		return -1;
	}

	if(timeout < 0){
		retu = semop(sem_id,&sem_b,1);
	}else if(timeout == 0){
		sem_b.sem_flg |= IPC_NOWAIT;
		retu = semop(sem_id,&sem_b,1);
	}else{
		struct timespec ts;
		ts.tv_sec = timeout/1000;
		ts.tv_nsec = (timeout%1000)*1000000;
		retu = semtimedop(sem_id,&sem_b,1,&ts);
	}
	if(retu == -1){
		close(fd);
		return retu;
	}

	//文件长度
	fstat(fd,&filestat);
	//读头
	if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
		lseek(fd,0,SEEK_SET);
		read(fd,head_buf,FILE_QUE_HEAD_SIZE);
	}
	//脏文件,清空
	if(filestat.st_size<FILE_QUE_HEAD_SIZE 
		||que_head->head_index<que_head->tail_index 
		|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
	){
		que_head->head_index = que_head->tail_index = 0;
		//ftruncate(fd,0);
	}
	//追加数据
	que_body->length = len;
	memcpy(que_body->data,pdata,len);
	retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE,SEEK_SET);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = write(fd,body_buf,FILE_QUE_BODY_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	//更新头
	que_head->head_index++;
	lseek(fd,0,SEEK_SET);
	retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = len;

	finish:
	//解锁
	sem_union.val = HAVETRANS;
	semctl(sem_id,0,SETVAL,sem_union);
	close(fd);
	return retu;
}

ssize_t read_file_que_timedwait_sem(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,sem_id;
	key_t sem_key;
	union semun sem_union;
	struct sembuf sem_b = {0,READTRANS,SEM_UNDO};//p操纵

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}

	sem_key = ftok(filename,1);
	sem_id = semget(sem_key,1,0664);
	if(sem_id == -1){
		//信号量不存在, 创建信号量
		if(errno == ENOENT){
			//锁文件
			fcntl(fd,F_SETLKW,&lock);
			sem_id = semget(sem_key,1,0664);
			if(sem_id == -1 && errno == ENOENT){
				sem_id = semget(sem_key,1,0664|IPC_CREAT);
				if(sem_id != -1){
					//设置初值
					sem_union.val = NOTRANS;
					semctl(sem_id,0,SETVAL,sem_union);
				}
			}
			//解锁文件
			fcntl(fd,F_SETLKW,&unlock);
		}
	}
	if(sem_id  == -1){
		close(fd);
		return -1;
	}
	
	if(timeout < 0){
		retu = semop(sem_id,&sem_b,1);
	}else if(timeout == 0){
		sem_b.sem_flg |= IPC_NOWAIT;
		retu = semop(sem_id,&sem_b,1);
	}else{
		struct timespec ts;
		ts.tv_sec = timeout/1000;
		ts.tv_nsec = (timeout%1000)*1000000;
		retu = semtimedop(sem_id,&sem_b,1,&ts);
	}
	if(retu == -1){
		close(fd);
		return retu;
	}
	
	//文件长度
	fstat(fd,&filestat);
	//读头
	if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
		lseek(fd,0,SEEK_SET);
		read(fd,head_buf,FILE_QUE_HEAD_SIZE);
	}
	//脏文件,清空
	if(filestat.st_size<FILE_QUE_HEAD_SIZE 
		||que_head->head_index<que_head->tail_index 
		|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
	){
		que_head->head_index = que_head->tail_index = 0;
		ftruncate(fd,0);
	}
	//有数据
	if(que_head->head_index > que_head->tail_index){
		//先偏移
		retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
		if(retu == -1){
			retu = -1;
			goto finish;
		}
		//有数据
		while(que_head->head_index > que_head->tail_index){
			retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
			if(retu == -1){
				retu = -1;
				break;
			}
			if(retu != FILE_QUE_BODY_SIZE){
				retu = -1;
				break;
			}
			if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
				//脏数据, 继续读
				retu = -1;
				que_head->tail_index++;
				continue;
			}
			if(que_body->length > len){
				//pdata不够长
				retu = -1;
				errno = E2BIG;
				break;
			}
			memcpy(pdata,que_body->data,que_body->length);

			que_head->tail_index++;
			if(que_head->head_index == que_head->tail_index){
				//队列为空, 清空文件
				que_head->head_index = que_head->tail_index = 0;
				ftruncate(fd,0);
			}else{
				lseek(fd,0,SEEK_SET);
				retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
			}
			retu = que_body->length;
			break;
		}
	}
	
	finish:
	if(que_head->head_index > que_head->tail_index){
		sem_union.val = HAVETRANS;
		semctl(sem_id,0,SETVAL,sem_union);
	}else{
		sem_union.val = NOTRANS;
	}
	semctl(sem_id,0,SETVAL,sem_union);

	close(fd);
	return retu;
}

ssize_t peek_file_que_timedwait_sem(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,sem_id;
	key_t sem_key;
	union semun sem_union;
	struct sembuf sem_b = {0,READTRANS,SEM_UNDO};//p操纵

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}

	sem_key = ftok(filename,1);
	sem_id = semget(sem_key,1,0664);
	if(sem_id == -1){
		//信号量不存在, 创建信号量
		if(errno == ENOENT){
			//锁文件
			fcntl(fd,F_SETLKW,&lock);
			sem_id = semget(sem_key,1,0664);
			if(sem_id == -1 && errno == ENOENT){
				sem_id = semget(sem_key,1,0664|IPC_CREAT);
				if(sem_id != -1){
					//设置初值
					sem_union.val = NOTRANS;
					semctl(sem_id,0,SETVAL,sem_union);
				}
			}
			//解锁文件
			fcntl(fd,F_SETLKW,&unlock);
		}
	}
	if(sem_id  == -1){
		close(fd);
		return -1;
	}
	
	if(timeout < 0){
		retu = semop(sem_id,&sem_b,1);
	}else if(timeout == 0){
		sem_b.sem_flg |= IPC_NOWAIT;
		retu = semop(sem_id,&sem_b,1);
	}else{
		struct timespec ts;
		ts.tv_sec = timeout/1000;
		ts.tv_nsec = (timeout%1000)*1000000;
		retu = semtimedop(sem_id,&sem_b,1,&ts);
	}
	if(retu == -1){
		close(fd);
		return retu;
	}
	
	//文件长度
	fstat(fd,&filestat);
	//读头
	if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
		lseek(fd,0,SEEK_SET);
		read(fd,head_buf,FILE_QUE_HEAD_SIZE);
	}
	//脏文件,清空
	if(filestat.st_size<FILE_QUE_HEAD_SIZE 
		||que_head->head_index<que_head->tail_index 
		|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
	){
		que_head->head_index = que_head->tail_index = 0;
		ftruncate(fd,0);
	}
	//有数据
	if(que_head->head_index > que_head->tail_index){
		//先偏移
		retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
		if(retu == -1){
			retu = -1;
			goto finish;
		}
		//有数据
		size_t tail_index_old = que_head->tail_index;
		while(que_head->head_index > que_head->tail_index){
			retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
			if(retu == -1){
				retu = -1;
				break;
			}
			if(retu != FILE_QUE_BODY_SIZE){
				retu = -1;
				break;
			}
			if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
				//脏数据, 继续读
				retu = -1;
				que_head->tail_index++;
				continue;
			}
			if(que_body->length > len){
				//pdata不够长
				retu = -1;
				errno = E2BIG;
				break;
			}
			memcpy(pdata,que_body->data,que_body->length);
			if(tail_index_old != que_head->tail_index){
				lseek(fd,0,SEEK_SET);
				retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
			}
			retu = que_body->length;
			break;
		}
	}
	
	finish:
	if(que_head->head_index > que_head->tail_index){
		sem_union.val = HAVETRANS;
		semctl(sem_id,0,SETVAL,sem_union);
	}else{
		sem_union.val = NOTRANS;
	}
	semctl(sem_id,0,SETVAL,sem_union);

	close(fd);
	return retu;
}
///////////////////////////////以上是信号量////////////////////////////////////////////////////////////

///////////////////////////////以下是fifo////////////////////////////////////////////////////////////
ssize_t write_file_que_timedwait_fifo(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,fifofd;
	char *fifo_filename=NULL;

	if(filename == NULL){
		return -1;
	}

	//pdata太长
	if(len > FILE_QUE_BODY_SIZE-sizeof(file_que_body)){
		return -1;
	}
	
	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1)
		return -1;

	//创建fifo
	fifo_filename = malloc(strlen(filename) + 6);
	sprintf(fifo_filename,"%s.fifo",filename);
	fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
	if(fifofd == -1){
		//fifo不存在, 创建fifo
		//锁文件
		fcntl(fd,F_SETLKW,&lock);
		fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		if(fifofd == -1){
			mkfifo(fifo_filename,0664);
			fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		}
		//解锁文件
		fcntl(fd,F_SETLKW,&unlock);
	}
	free(fifo_filename);

	if(fifofd == -1){
		close(fd);
		return -1;
	}

	//锁文件
	fcntl(fd,F_SETLKW,&lock);

	//通知
	write(fifofd,"\x00",1);

	//文件长度
	fstat(fd,&filestat);
	//读头
	if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
		lseek(fd,0,SEEK_SET);
		read(fd,head_buf,FILE_QUE_HEAD_SIZE);
	}
	//脏文件,清空
	if(filestat.st_size<FILE_QUE_HEAD_SIZE 
		||que_head->head_index<que_head->tail_index 
		|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
	){
		que_head->head_index = que_head->tail_index = 0;
		//ftruncate(fd,0);
	}
	//追加数据
	que_body->length = len;
	memcpy(que_body->data,pdata,len);
	retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE,SEEK_SET);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = write(fd,body_buf,FILE_QUE_BODY_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	//更新头
	que_head->head_index++;
	lseek(fd,0,SEEK_SET);
	retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
	if(retu == -1){
		retu = -1;
		goto finish;
	}
	retu = len;

	finish:
	//解锁
	fcntl(fd,F_SETLKW,&unlock);
	close(fifofd);
	close(fd);
	return retu;
}

ssize_t read_file_que_timedwait_fifo(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,fifofd;
	char *fifo_filename=NULL;
	uint64_t now,expire;
	unsigned char tmp[10] = {0};

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}

	//创建fifo
	fifo_filename = malloc(strlen(filename) + 6);
	sprintf(fifo_filename,"%s.fifo",filename);
	fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
	if(fifofd == -1){
		//fifo不存在, 创建fifo
		//锁文件
		fcntl(fd,F_SETLKW,&lock);
		fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		if(fifofd == -1){
			mkfifo(fifo_filename,0664);
			fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		}
		//解锁文件
		fcntl(fd,F_SETLKW,&unlock);
	}
	free(fifo_filename);

	if(fifofd == -1){
		close(fd);
		return -1;
	}

	_localtime(&now);
	if(timeout < 0){
		expire = -1;
	}else{
		expire = now + timeout * 1000000;
	}
	
	while(now <= expire){
		memset(head_buf,0,sizeof(head_buf));
		//加锁
		fcntl(fd,F_SETLKW,&lock);
		//文件长度
		fstat(fd,&filestat);
		//读头
		if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
			lseek(fd,0,SEEK_SET);
			read(fd,head_buf,FILE_QUE_HEAD_SIZE);
		}
		//脏文件,清空
		if(filestat.st_size<FILE_QUE_HEAD_SIZE 
			||que_head->head_index<que_head->tail_index 
			|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
		){
			que_head->head_index = que_head->tail_index = 0;
			ftruncate(fd,0);
		}
		//有数据
		if(que_head->head_index > que_head->tail_index){
			//先偏移
			retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
			if(retu == -1){
				retu = -1;
				break;
			}
			//有数据
			while(que_head->head_index > que_head->tail_index){
				retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
				if(retu != FILE_QUE_BODY_SIZE){
					retu = -1;
					break;
				}
				if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
					//脏数据, 继续读
					retu = -1;
					que_head->tail_index++;
					continue;
				}
				if(que_body->length > len){
					//pdata不够长
					retu = -1;
					errno = E2BIG;
					break;
				}
				memcpy(pdata,que_body->data,que_body->length);

				que_head->tail_index++;
				if(que_head->head_index == que_head->tail_index){
					//队列为空, 清空文件
					que_head->head_index = que_head->tail_index = 0;
					ftruncate(fd,0);
				}else{
					lseek(fd,0,SEEK_SET);
					retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
					if(retu == -1){
						retu = -1;
						break;
					}
				}
				retu = que_body->length;
				break;
			}
		}
		//解锁
		fcntl(fd,F_SETLKW,&unlock);

		//取到数据或者读数据有某些问题的，可以返回了
		if(retu >= 0 || que_head->head_index > que_head->tail_index){
			break;
		}
		//队列为空
		if(now >= expire){
			retu = -1;
			break;
		}

		//无数据,则用fifo+select等待文件写入
		struct timeval tv,*tv_p;
		if(timeout < 0){
			tv_p = NULL;
		}else{
			tv_p = &tv;
			tv_p->tv_sec = (expire - now) / 1000000;
			tv_p->tv_usec = (expire - now) % 1000000;
		}

		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(fifofd,&rset);
		int n = select(fifofd+1,&rset,NULL,NULL,tv_p);
		
		if(n == 0){
			//timeout
			errno = ETIME;
			retu = -1;
			break;
		}else if(n == -1){
			//signal or other
			retu = -1;
			break;
		}else{
			//继续读文件
			read(fifofd,tmp,1);
		}
		_localtime(&now);
	}
	close(fifofd);
	close(fd);
	return retu;
}

ssize_t peek_file_que_timedwait_fifo(const char* filename, void *pdata, size_t len, int timeout){
	struct flock lock = {F_WRLCK,SEEK_SET,0,0};
	struct flock unlock = {F_UNLCK,SEEK_SET,0,0};

	ssize_t retu = -1;
	struct stat filestat;
	file_que_head *que_head;
	file_que_body *que_body;
	unsigned char head_buf[FILE_QUE_HEAD_SIZE] = {0};
	unsigned char body_buf[FILE_QUE_BODY_SIZE] = {0};
	que_head = (file_que_head*)head_buf;
	que_body = (file_que_body*)body_buf;
	int fd,fifofd;
	char *fifo_filename=NULL;
	uint64_t now,expire;
	unsigned char tmp[10] = {0};

	if(filename == NULL){
		return -1;
	}

	fd = open(filename,O_RDWR|O_CREAT,0664);
	if(fd == -1){
		return -1;
	}

	//创建fifo
	fifo_filename = malloc(strlen(filename) + 6);
	sprintf(fifo_filename,"%s.fifo",filename);
	fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
	if(fifofd == -1){
		//fifo不存在, 创建fifo
		//锁文件
		fcntl(fd,F_SETLKW,&lock);
		fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		if(fifofd == -1){
			mkfifo(fifo_filename,0664);
			fifofd = open(fifo_filename,O_RDWR|O_NONBLOCK);
		}
		//解锁文件
		fcntl(fd,F_SETLKW,&unlock);
	}
	free(fifo_filename);

	if(fifofd == -1){
		close(fd);
		return -1;
	}

	_localtime(&now);
	if(timeout < 0){
		expire = -1;
	}else{
		expire = now + timeout * 1000000;
	}

	while(now <= expire){
		memset(head_buf,0,sizeof(head_buf));
		//加锁
		fcntl(fd,F_SETLKW,&lock);
		//文件长度
		fstat(fd,&filestat);
		//读头
		if(filestat.st_size >= FILE_QUE_HEAD_SIZE){
			lseek(fd,0,SEEK_SET);
			read(fd,head_buf,FILE_QUE_HEAD_SIZE);
		}
		//脏文件,清空
		if(filestat.st_size<FILE_QUE_HEAD_SIZE 
			||que_head->head_index<que_head->tail_index 
			|| FILE_QUE_HEAD_SIZE + que_head->head_index * FILE_QUE_BODY_SIZE != filestat.st_size
		){
			que_head->head_index = que_head->tail_index = 0;
			ftruncate(fd,0);
		}
		//有数据
		size_t tail_index_old = que_head->tail_index;
		if(que_head->head_index > que_head->tail_index){
			//先偏移
			retu = lseek(fd,FILE_QUE_HEAD_SIZE + que_head->tail_index * FILE_QUE_BODY_SIZE,SEEK_SET);
			if(retu == -1){
				retu = -1;
				break;
			}
			//有数据
			while(que_head->head_index > que_head->tail_index){
				retu = read(fd,body_buf,FILE_QUE_BODY_SIZE);
				if(retu == -1){
					retu = -1;
					break;
				}
				if(retu != FILE_QUE_BODY_SIZE){
					retu = -1;
					break;
				}
				if(que_body->length > FILE_QUE_BODY_SIZE-sizeof(file_que_head)){
					//脏数据, 继续读
					retu = -1;
					que_head->tail_index++;
					continue;
				}
				if(que_body->length > len){
					//pdata不够长
					retu = -1;
					errno = E2BIG;
					break;
				}
				memcpy(pdata,que_body->data,que_body->length);
				if(tail_index_old != que_head->tail_index){
					lseek(fd,0,SEEK_SET);
					retu = write(fd,head_buf,FILE_QUE_HEAD_SIZE);
					if(retu == -1){
						retu = -1;
						break;
					}
				}
				retu = que_body->length;
				break;
			}
		}
		//解锁
		fcntl(fd,F_SETLKW,&unlock);

		//取到数据或者读数据有某些问题的，可以返回了
		if(retu >= 0 || que_head->head_index > que_head->tail_index){
			break;
		}
		//队列为空
		if(now >= expire){
			retu = -1;
			break;
		}

		//无数据,则用fifo+select等待文件写入
		struct timeval tv,*tv_p;
		if(timeout < 0){
			tv_p = NULL;
		}else{
			tv_p = &tv;
			tv_p->tv_sec = (expire - now) / 1000000;
			tv_p->tv_usec = (expire - now) % 1000000;
		}

		fd_set rset;
		FD_ZERO(&rset);
		FD_SET(fifofd,&rset);
		int n = select(fifofd+1,&rset,NULL,NULL,tv_p);
		
		if(n == 0){
			//timeout
			errno = ETIME;
			retu = -1;
			break;
		}else if(n == -1){
			//signal or other
			retu = -1;
			break;
		}else{
			//继续读文件
			read(fifofd,tmp,1);
		}
		_localtime(&now);
	}
	close(fifofd);
	close(fd);
	return retu;
}
///////////////////////////////以上是fifo////////////////////////////////////////////////////////////