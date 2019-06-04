#!/bin/bash

ps aux | grep mytask | grep -v grep | cut -c 9-15 | xargs kill -9
# 启动realsense检测服务
cmd=$(pgrep -fc mytask)
if [[ $cmd -lt 1 ]]
then
	nohup /home/pi/git/G58PlayGameRastbarryToServer/mytask > /home/pi/git/G58PlayGameRastbarryToServer/message.log 2>&1 &
fi
