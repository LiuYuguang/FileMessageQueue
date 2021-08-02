#ifndef _FILEMQ_H_
#define _FILEMQ_H_

#include <sys/types.h> //for key_t
#include <stddef.h>    //for size_t
#include <sys/types.h> //for ssize_t


/**
 * @brief 写队列数据
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t write_file_que_timedwait_inotify(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t read_file_que_timedwait_inotify(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 但不将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t peek_file_que_timedwait_inotify(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 写队列数据
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t write_file_que_timedwait_sem(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t read_file_que_timedwait_sem(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 不将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t peek_file_que_timedwait_sem(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 写队列数据
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t write_file_que_timedwait_fifo(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t read_file_que_timedwait_fifo(const char* filename,void* pdata,size_t len,int timeout);

/**
 * @brief 读队列数据, 不将数据移除
 * @param[in] filename
 * @param[in] pdata
 * @param[in] len
 * @param[in] maximum wait time in microseconds (-1 == infinite)
 * @return >=0 if successful, <0 error
*/
ssize_t peek_file_que_timedwait_fifo(const char* filename,void* pdata,size_t len,int timeout);

#endif