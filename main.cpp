#include <iostream>
#include <serial/serial.h>

#include "common.h"
#include "ClientSocket.h"

#define SERVER_IP "192.168.1.100"
using namespace std;

#define COORDINATOR_DEV_NAME "/dev/ttyUSB0"
serial::Serial coorDevSerial; // 定义连接树莓派协调器的串口对象
pthread_t serial_port_thread, serial_port_thread2;
int serialSocketfd;

bool myopen_port(serial::Serial& ser, std::string port_name, int baudrate);
void *serial_data_process_thread(void* ptr);
void *serial_data_process_thread2(void* ptr);

int main(int argc, char* argv[]) 
{
    int ret = 0;

    // 1.初始化socket连接服务器
    cout << "connect to server ip = " << SERVER_IP << endl;
    while(1) {
        ret = init_tcp_socket_client_block(&serialSocketfd, SERVER_IP.c_str(), 9877); // 192.168.2.105  www.g58mall.com
        if(ret < 0) {
            cout << "Connect to server failed, try again" << endl;
            sleep(5);
            continue;
        } else {
            cout << "Connect to server success" << endl;
            break;
        }
    }

    // 2.初始化zigbee协调器串口
    bool open_flag = myopen_port(coorDevSerial, COORDINATOR_DEV_NAME, 115200);
	if(!open_flag){
		cout << "Open coor serial port failed, exit" << endl;
        exit(-1);
	} else {
        cout << "Open coor serial port success" << endl;
    }

    // 3. 创建一个线程用于接收协调器上传的数据并发送到服务器
    if(pthread_create(&serial_port_thread, 0, serial_data_process_thread, 0)){
		cout << "Create coor pthread failed" << endl;
        exit(-1);
	}else {
		cout << "Create coor pthread success" << endl;
	}

    // 4.接收socket服务器的数据，串口转发到zigbee
    if(pthread_create(&serial_port_thread2, 0, serial_data_process_thread2, 0)){
		cout << "Create recv socket data thread failed" << endl;
        exit(-1);
	}else {
		cout << "Create recv socket data thread success" << endl;
	}
    
}

/**
 * 打开zigbee协调器串口
 * */
bool myopen_port(serial::Serial& ser, std::string port_name, int baudrate){
  try{    //设置串口属性，并打开串口
      ser.setPort(port_name);
      ser.setBaudrate(baudrate);
      serial::Timeout timeOut = serial::Timeout::simpleTimeout(50);
      ser.setTimeout(timeOut);
      ser.open();
    }
    catch (serial::IOException& e)
    {
        cout << "Cannot open serial port = " << port_name << endl;
        return false;
    }
    if(ser.isOpen()){//检测串口是否已经打开，并给出提示信息
        cout << "Serial port has opened" << endl;
    }else{
        return false;
    }
    return true;
}

/**
 * zigbee串口线程
 * 功能：将串口发送上来的数据原样发送到服务器
*/
void *serial_data_process_thread(void* ptr) {
	char buffer[128] = {0};
	unsigned char data_buf[64] = {0};
	while(1) {
		bzero(data_buf, 64);
		while(coorDevSerial.available() > 0) {
			int rd_len = coorDevSerial.read(data_buf, coorDevSerial.available());
            cout << "recv coor serialport dataLen = " << rd_len << ", = " << data_buf << endl;
			sendTcpMsg(serialSocketfd, (char*)data_buf, rd_len); // 将接收到的数据发送到服务器
		}		
		usleep(1000*10); // 休眠ms
	}
	cout << "Exit serial_data_process_thread" << endl;
}

/**
 * zigbee串口线程2
 * 功能：将服务器发送的控制命令发送到串口
*/
void *serial_data_process_thread2(void* ptr) {
	char buffer[128] = {0};
	while(1) {
		// 接收服务器数据
		if(serialSocketfd == -1) {
			sleep(2);
			continue;
		}
		bzero(buffer, 128);
		int len = recv(serialSocketfd, buffer, 128, MSG_DONTWAIT); // 非阻塞接收
		if(len > 0) { // 服务器端发送数据不能太频繁，否则数据会累积
            cout << "recv socket server data len = " << len << ", data = " << buffer << endl;
			int ret = goodsDevSerial.write((unsigned char*)buffer, len);
			if(ret != len) {
				cout << "Thread send socketdata to serial failed" << endl;
			}
		} else {
			usleep(1000*10); // 休眠ms
		}		
	}
	cout << "Exit serial_data_process_thread2" << endl;
}