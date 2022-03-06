#ifndef _FILEMQ_H_
#define _FILEMQ_H_

#include <sys/types.h> //for key_t
#include <stddef.h>    //for size_t
#include <sys/types.h> //for ssize_t

typedef struct _FileMQ FileMQ;

/*
 * @brief 初始化
 * @param[in] filename
 * @param[in] capacity
*/
FileMQ *filemq_init(const char* filename,size_t capacity);

/**
 * @brief 写队列数据
 * @param[in] filemq
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t write_filemq(FileMQ* filemq,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 将数据移除
 * @param[in] filemq
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t read_filemq(FileMQ* filemq,void* pdata,size_t size,int timeout);

/**
 * @brief 读队列数据, 不将数据移除
 * @param[in] filemq
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
// ssize_t peek_file(FileMQ* filemq,void* pdata,size_t len,int timeout);

#endif