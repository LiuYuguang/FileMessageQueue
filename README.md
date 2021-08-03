# 用三种方法实现文件消息队列  
1. inotify + 记录锁  
2. 信号量  
3. fifo + 记录锁  

# 测试环境
树莓派4B
```bash
>> cd /dev/shm
>> time dd if=/dev/zero of=tmp bs=4k count=10000
10000+0 records in
10000+0 records out
40960000 bytes (41 MB) copied, 0.0912378 s, 449 MB/s

real    0m0.110s
user    0m0.004s
sys     0m0.106s
```

# 测试
```bash
>> cd /dev/shm/FileMessageQueue
>> ./bench.sh
test file_que_inotify_bench #inotify + 记录锁, 单位为微秒
测试触发延迟, 次数10000, total_count[10000],total_diff[49991065],min_diff[64],max_diff[23151],average_diff[4999]
测试只写耗时, 次数10000, 耗时331486
测试只读耗时, 次数10000, 耗时453488
test file_que_sem_bench #信号量, 单位为微秒
测试触发延迟, 次数10000, total_count[10000],total_diff[732318],min_diff[60],max_diff[4050],average_diff[73]
测试只写耗时, 次数10000, 耗时349859
测试只读耗时, 次数10000, 耗时327223
test file_que_fifo_bench #fifo + 记录锁, 单位为微秒
测试触发延迟, 次数10000, total_count[10000],total_diff[1040940],min_diff[71],max_diff[3663],average_diff[104]
测试只写耗时, 次数10000, 耗时496884
测试只读耗时, 次数10000, 耗时434965
```