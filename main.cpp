#include <iostream>
#include <serial/serial.h>

#include "common.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "ClientSocket.h"
#ifdef __cplusplus
}
#endif
using namespace std;

#define COORDINATOR_DEV_NAME "/dev/ttyUSB0"
serial::Serial coorDevSerial; // 定义连接树莓派协调器的串口对象
pthread_t serial_port_thread, serial_port_thread2;
int serialSocketfd;
struct timeval last_sendheartbeat_tv;
struct timeval last_recvheartbeat_tv; // 上一次收到心跳的时间
struct timeval current_tv;
string MACHINE_ID = "1";

bool myopen_port(serial::Serial& ser, std::string port_name, int baudrate);
void *serial_data_process_thread(void* ptr);
void *serial_data_process_thread2(void* ptr);
int sendSerialDateToServer(string content);
void SplitString(const string& s, vector<string>& v, const string& c);

void testStirngSplit(void);

int main(int argc, char* argv[]) 
{
    int ret = 0;
    string SERVER_IP = "192.168.1.100";
    int server_port = 9878;
    fstream file("./server_ip.conf");//创建一个fstream文件流对象
	getline(file, SERVER_IP);
    getline(file, MACHINE_ID);
    file.close();
    // 1.初始化socket连接服务器
    cout << "connect to server ip = " << SERVER_IP << ", MACHINE_ID = " << MACHINE_ID << endl;

    while(1) {
        ret = init_tcp_socket_client_block(&serialSocketfd, SERVER_IP.c_str(), server_port); // 192.168.2.105  www.g58mall.com
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

    // 5.发送socket初始注册消息
    ret = sendSerialDateToServer("reg");
	if(ret > 0) {
		cout << "Send socket register cmd success" << endl;
	}

    // 6.初始化计时器
    gettimeofday(&current_tv,NULL);
    gettimeofday(&last_sendheartbeat_tv, NULL);    
	gettimeofday(&last_recvheartbeat_tv, NULL); // 更新心跳计时

    while(1) {
        // 每隔一秒钟发送一次心跳到服务器
        gettimeofday(&current_tv, NULL);
		if((current_tv.tv_sec - last_sendheartbeat_tv.tv_sec) >= 1) {
			sendSerialDateToServer("heartbeat"); // 发送货柜串口心跳
			gettimeofday(&last_sendheartbeat_tv, NULL);
		}

        // 及时判断服务器是否断开了连接，断开之后能自定进行连接
        gettimeofday(&current_tv, NULL);
		if((current_tv.tv_sec - last_recvheartbeat_tv.tv_sec) >= 5) { // 如果当前收到服务器的心跳超过指定时间，则认为断开连接，进行重连操作
			serialSocketfd = -1;
			perror("Recv server heartbeat time-out");
			close(serialSocketfd); // 关闭文件描述符
			int sock_ret = init_tcp_socket_client_block(&serialSocketfd, (char*)SERVER_IP.c_str(), server_port);
			if(sock_ret < 0) {
				cout << "Reconnect to server failed" << endl;
			}else{ // 连接成功，发送货柜串口注册消息
				cout << "Reconenct to server success, send register cmd again" << endl;
				sendSerialDateToServer("reg");
				sendSerialDateToServer("heartbeat"); // 发送货柜串口心跳
			}            
			gettimeofday(&last_recvheartbeat_tv, NULL); // 更新时间，避免断开之后不停地重连
		}
        usleep(1000*20);
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
	uint8_t data_buf[64] = {0};
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
    int ret = 0;
	while(1) {
		// 接收服务器数据
		if(serialSocketfd == -1) {
			sleep(2);
			continue;
		}
		bzero(buffer, 128);
		int len = recv(serialSocketfd, buffer, 128, 0);
		if(len > 0) { // 服务器端发送数据不能太频繁，否则数据会累积
            cout << "recv socket server data len = " << len << ", data = " << buffer << endl;
            string str = buffer;
            vector<string> vec;
            SplitString(str, vec, "@"); // 按照字符进行分割，防止数据粘连
            for(vector<string>::size_type i = 0; i < vec.size(); i++){
                if(vec[i].compare("#heartbeat@") == 0) { // 如果收到的是心跳数据
                    gettimeofday(&last_recvheartbeat_tv, NULL); // 更新心跳计时
                } else { // 将数据发送到协调器
                    int strLen = vec[i].length();
                    ret = coorDevSerial.write((const uint8_t*)(vec[i].c_str()), strLen);
                    if(ret != strLen) {
                        cout << "Thread send socketdata to serial failed" << endl;
                    }
                }
            }                			
		} else {
			usleep(1000*10); // 休眠ms
		}		
	}
	cout << "Exit serial_data_process_thread2" << endl;
}

/**
 * 进行string字符串拆分
*/
void SplitString(const string& s, vector<string>& v, const string& c)
{
    string::size_type pos1, pos2;
    pos1 = 0;
    pos2 = s.find(c); // 查找字符串s中是否包含字符串c，string::npos是个特殊值
    while(string::npos != pos2)
    {
        v.push_back(s.substr(pos1, pos2-pos1+c.size()));
         
        pos1 = pos2 + c.size();
        pos2 = s.find(c, pos1);
    }
    if(pos1 != s.length())
        v.push_back(s.substr(pos1));
}

int sendSerialDateToServer(string content) {
	char sendBuf[64] = {0};
	sprintf(sendBuf, "#{\"machineId\":\"%s\",\"type\":\"%s\"}@", MACHINE_ID.c_str(), content.c_str());
	return sendTcpMsg(serialSocketfd, sendBuf, strlen(sendBuf));
}

void testStirngSplit(void) {
    string s = "#fjdfjkdjgf@#rrrrrrrrrrr@#llhhhhhhh";
    vector<string> v;
    SplitString(s, v,"@"); //可按多个字符来分隔;
    for(vector<string>::size_type i = 0; i != v.size(); ++i)
        cout << v[i] << "; ";
    cout << endl;
}