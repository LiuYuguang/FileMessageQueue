rm tmp
echo "test file_que_inotify_bench"
./file_que_inotify_bench $@
sleep 1

rm tmp
echo "test file_que_sem_bench"
./file_que_sem_bench $@
sleep 1

rm tmp
echo "test file_que_fifo_bench"
./file_que_fifo_bench $@