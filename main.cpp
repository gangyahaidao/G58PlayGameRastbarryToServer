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

#define uint8 unsigned char
#define HEAD_BYTE 0x7E
#define RASTBERRY_REG 0x10 // 树莓派注册命令
#define RASTBERRY_HEART_BEAT 0x11 // 树莓派心跳
#define REPLY_RASTBERRY_HEART_BEAT 0x12 // 服务器回复的树莓派心跳

#define COORDINATOR_DEV_NAME "/dev/ttyUSB0"
serial::Serial coorDevSerial; // 定义连接树莓派协调器的串口对象
pthread_t serial_port_thread, serial_port_thread2;
int serialSocketfd;
struct timeval last_sendheartbeat_tv;
struct timeval last_recvheartbeat_tv; // 上一次收到心跳的时间
struct timeval current_tv;
string MACHINE_ID = "1";

// 互斥锁变量
int semid;

bool myopen_port(serial::Serial& ser, std::string port_name, int baudrate);
void *serial_data_process_thread(void* ptr);
void *serial_data_process_thread2(void* ptr);
int sendRaspDataToServer(uint8 cmd);
void SplitString(const string& s, vector<string>& v, const string& c);

void encodeData(uint8 cmd, uint8* content, uint8 contentLen, uint8* outputBuf, uint8* outputLen);

uint8* re_replace_data(uint8* buffer, uint8 length, uint8* destArr, uint8* destLengthPtr);
bool check_xor(uint8* destArr, uint8 length);

void del_semvalue(int sem_id);
int semaphore_p(int sem_id);
int semaphore_v(int sem_id);
int sendTcpDataWithSemph(int clientSocketfd, char* sendServerMsg, int len);

int main(int argc, char* argv[]) 
{
    int ret = 0;
    string SERVER_IP = "192.168.1.103";
    int server_port = 18888;
    fstream file("./server_ip.conf");//创建一个fstream文件流对象
	getline(file, SERVER_IP);
    getline(file, MACHINE_ID);
    file.close();

    // 创建一个二值信号量用于socket发送数据互斥
    if((semid=semget((key_t)123456,1,0666|IPC_CREAT)) == -1)
	{//创建一个二值信号量,用于进程间同步
		printf("semget() error");
		exit(EXIT_FAILURE);
	}
	if (semctl(semid, 0, SETVAL, 1) == -1)//信号量初始化为1，为可用状态
	{// 用于初始化信号量
		printf("sem init error");
        if(semctl(semid,0,IPC_RMID,0) != 0)
		{
            printf("semctl() IPC_RMID error");
		}
        exit(EXIT_FAILURE);
	}

    // 1.初始化socket连接服务器
    cout << "connect to server ip = " << SERVER_IP << ", port = " << server_port << ", MACHINE_ID = " << MACHINE_ID << endl;
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
    while(1) {
        bool open_flag = myopen_port(coorDevSerial, COORDINATOR_DEV_NAME, 115200);
        if(!open_flag){
            cout << "Open coor serial port failed, exit" << endl;
            coorDevSerial.close();
            sleep(5);
            continue;
        } else {
            cout << "Open coor serial port success" << endl;
            break;
        }
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
    ret = sendRaspDataToServer(RASTBERRY_REG);
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
            // 发送树莓派心跳到服务器
            sendRaspDataToServer(RASTBERRY_HEART_BEAT);
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
				sendRaspDataToServer(RASTBERRY_REG);
				sendRaspDataToServer(RASTBERRY_HEART_BEAT); // 发送货柜串口心跳
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
			sendTcpDataWithSemph(serialSocketfd, (char*)data_buf, rd_len); // 将接收到的数据原样发送到服务器
            cout << "recv coor serialport dataLen = " << rd_len << ", = " << endl;
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
	uint8 byteValue = 0;
    int ret = 0;
    bool recv_head = false;
    bool recv_tail = false;
    uint8 buffer[128] = {0};    
    uint8 recvLen = 0;
    uint8 outputBuffer[128] = {0};
    uint8 outputLen = 0;

	while(1) {
		// 接收服务器数据
		if(serialSocketfd == -1) {
			sleep(2);
			continue;
		}
		int len = recv(serialSocketfd, &byteValue, 1, 0);
		if(len > 0) {
            cout << "recv socket server data len = " << len << ", byteValue = " << byteValue << endl;
            if(byteValue == HEAD_BYTE) {
                if(!recv_head) { // 第一次收到开始标识
                    recv_head = true;
                } else {
                    recv_tail = true;
                }
            }
            buffer[recvLen++] = byteValue;
            if(recv_head && recv_tail) { // 收到一帧完整的数据                
                re_replace_data(buffer, recvLen-1, outputBuffer, &outputLen); // 还原被替换的特殊数据
                bool check = check_xor(outputBuffer, outputLen); // 数据校验
                if(check) {
                    uint8 cmd = outputBuffer[1];
                    if(cmd == REPLY_RASTBERRY_HEART_BEAT) { // 服务器回复的心跳命令
                        gettimeofday(&last_recvheartbeat_tv, NULL); // 更新连接服务器的心跳计时
                    } else { // 是发送到协调器的数据
                        ret = coorDevSerial.write(outputBuffer, outputLen);
                        if(ret != outputLen) {
                            cout << "Thread send socketdata to serial failed" << endl;
                        }
                    }                    
                } else {
                    cout << "check failed" << endl;
                }
                recv_head = false;
                recv_tail = false;
                bzero(buffer, 128);
                bzero(outputBuffer, 128);
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

#define SEND_BUF_SIZE 128
int sendRaspDataToServer(uint8 cmd) {
	char sendBuf[SEND_BUF_SIZE] = {0};
    uint8 resultBuf[SEND_BUF_SIZE] = {0};
    uint8 sendDataLen = 0;

    if(cmd == RASTBERRY_REG) { // 树莓派注册命令
        sprintf(sendBuf, "{\"machineId\":\"%s\"}", MACHINE_ID.c_str());
        encodeData(RASTBERRY_REG, (uint8*)sendBuf, strlen(sendBuf), resultBuf, &sendDataLen); // 组装数据
        return sendTcpDataWithSemph(serialSocketfd, (char*)resultBuf, sendDataLen); // 将组装之后的数据发送出去
    } else if(cmd == RASTBERRY_HEART_BEAT) { // 树莓派发送心跳命令
        encodeData(RASTBERRY_HEART_BEAT, NULL, 0, resultBuf, &sendDataLen); // 组装数据
        return sendTcpDataWithSemph(serialSocketfd, (char*)resultBuf, sendDataLen); // 将组装之后的数据发送出去
    }
}

/**
 * 按照协议封装数据
 * Data Format: 0x7E + 1byteCmd + encType + 1byteLength + data[] + xor + 0x7E
*/
void encodeData(uint8 cmd, uint8* content, uint8 contentLen, uint8* outputBuf, uint8* outputLen) {
    uint8 sendBuf[SEND_BUF_SIZE] = {0};
    int index = 0;

    sendBuf[index++] = 0x7E;
    sendBuf[index++] = cmd;
    sendBuf[index++] = 0x0; // 加密方式0x0表示不加密
    sendBuf[index++] = contentLen; // 消息体长度
    if(content != NULL) {
        memcpy(sendBuf+index, content, contentLen);
        index += contentLen;
    }    
    // calculate xor
    printf("index1 = %d\n", index);
    uint8 XOR = sendBuf[1];
    int i = 0;
    for(i = 2; i < index-1; i++) {
        XOR ^= sendBuf[i];
        printf("i = %d, XOR = %d\n", i, XOR);
    }
    sendBuf[index++] = XOR;
    sendBuf[index++] = 0x7E;
    printf("data length = %d\n", index);

    for(i = 0; i < index; i++) {
        printf("buf[%d] = 0x%x\n", i, sendBuf[i]);
    }

    uint8 tmpOutLen = 0;
    outputBuf[tmpOutLen++] = 0x7E;
    for(i = 1; i < index-1; i++) {
        if(sendBuf[i] == 0x7E) {
            outputBuf[tmpOutLen++] = 0x7D;
            outputBuf[tmpOutLen++] = 0x02;
        } else if(sendBuf[i] == 0x7D) {
            outputBuf[tmpOutLen++] = 0x7D;
            outputBuf[tmpOutLen++] = 0x01;
        } else {
            outputBuf[tmpOutLen++] = sendBuf[i];
        }
    }
    outputBuf[tmpOutLen] = 0x7E;
    *outputLen = tmpOutLen;
}

/**
 * 解析服务器发送过来的数据，步骤一：数据替换
*/
uint8* re_replace_data(uint8* buffer, uint8 length, uint8* destArr, uint8* destLengthPtr){
    int i = 0;
    int j = 0;
    for(i = 1; i < length-1; i++) {
        if(buffer[i] == 0x7D && buffer[i+1] == 0x02) {
            destArr[j++] = 0x7E;
            i++;
        } else if(buffer[i] == 0x7D && buffer[i+1] == 0x01) {
            destArr[j++] = 0x7D;
            i++;
        } else {
            destArr[j++] = buffer[i];
        }
    }
    *destLengthPtr = j-1;

    return destArr;
}
/**
 * 解析服务器发送过来的数据，步骤二：数据校验
*/
bool check_xor(uint8* destArr, uint8 length) {
    uint8 XOR = destArr[length-2];
    uint8 contentLen = destArr[3];

    if(contentLen > 0) {
        uint8 calculate = destArr[1];
        int i = 0;
        for(i = 2; i < length-2; i++) {
            calculate = calculate ^ destArr[i];
        }
        if(XOR != calculate) {
            return false;
        }
    }
    return true;
}

void del_semvalue(int sem_id)
{
    // 删除信号量
    if (semctl(sem_id, 0, IPC_RMID, 0) == -1)
    {
        perror("Failed to delete semaphore");
    }
}
int semaphore_p(int sem_id)
{
    // 对信号量做减1操作，即等待P（sv）
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = -1;//P()
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1)
    {
        perror("semaphore_p failed");
    }

    return 1;
}
int semaphore_v(int sem_id)
{
    // 这是一个释放操作，它使信号量变为可用，即发送信号V（sv）
    struct sembuf sem_b;
    sem_b.sem_num = 0;
    sem_b.sem_op = 1; // V()
    sem_b.sem_flg = SEM_UNDO;
    if (semop(sem_id, &sem_b, 1) == -1)
    {
        perror("semaphore_v failed");
    }

    return 1;
}
/**
 * 增加互斥功能的tcp发送函数，防止数据混乱
*/
int sendTcpDataWithSemph(int clientSocketfd, char* sendServerMsg, int len) {    
    // 进入临界区
    if (!semaphore_p(semid))
    {
        exit(EXIT_FAILURE);
    }
        int ret = sendTcpMsg(clientSocketfd, sendServerMsg, len);
    // 离开临界区
    if (!semaphore_v(semid))
    {
        exit(EXIT_FAILURE);
    }
    return ret;
} 