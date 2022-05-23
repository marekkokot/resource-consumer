#!/bin/bash

#https://dev.to/rrampage/surviving-the-linux-oom-killer-2ki9

params=$*
bin/resource-consumer $params &
pid=$!
echo $! 
echo "pid: $pid"
echo -800 | sudo tee - /proc/$pid/oom_score_adj

while true
do	
	sleep 1
done
kill $pid
