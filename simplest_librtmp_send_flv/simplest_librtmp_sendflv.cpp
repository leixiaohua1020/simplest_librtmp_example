/**
 * Simplest Librtmp Send FLV
 *
 * 雷霄骅，张晖
 * leixiaohua1020@126.com
 * zhanghuicuc@gmail.com
 * 中国传媒大学/数字电视技术
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * 本程序用于将FLV格式的视音频文件使用RTMP推送至RTMP流媒体服务器。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#ifndef WIN32
#include <unistd.h>
#endif


#include "librtmp/rtmp_sys.h"
#include "librtmp/log.h"


#define HTON16(x)  ((x>>8&0xff)|(x<<8&0xff00))
#define HTON24(x)  ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00))
#define HTON32(x)  ((x>>24&0xff)|(x>>8&0xff00)|\
	(x<<8&0xff0000)|(x<<24&0xff000000))
#define HTONTIME(x) ((x>>16&0xff)|(x<<16&0xff0000)|(x&0xff00)|(x&0xff000000))

int ReadU8(uint32_t *u8,FILE*fp){
	if(fread(u8,1,1,fp)!=1)
		return 0;
	return 1;
}
int ReadU16(uint32_t *u16,FILE*fp){
	if(fread(u16,2,1,fp)!=1)
		return 0;
	*u16=HTON16(*u16);
	return 1;
}
int ReadU24(uint32_t *u24,FILE*fp){
	if(fread(u24,3,1,fp)!=1)
		return 0;
	*u24=HTON24(*u24);
	return 1;
}
int ReadU32(uint32_t *u32,FILE*fp){
	if(fread(u32,4,1,fp)!=1)
		return 0;
	*u32=HTON32(*u32);
	return 1;
}
int PeekU8(uint32_t *u8,FILE*fp){
	if(fread(u8,1,1,fp)!=1)
		return 0;
	fseek(fp,-1,SEEK_CUR);
	return 1;
}
int ReadTime(uint32_t *utime,FILE*fp){
	if(fread(utime,4,1,fp)!=1)
		return 0;
	*utime=HTONTIME(*utime);
	return 1;
}

int InitSockets()
{
	WORD version;
	WSADATA wsaData;
	version=MAKEWORD(2,2);
	return (WSAStartup(version, &wsaData) == 0);
}

void CleanupSockets()
{
	WSACleanup();
}

int send_use_packet(){

	RTMP *rtmp=NULL;//rtmp应用指针
	RTMPPacket *packet=NULL;//rtmp包结构
	long start=0;
	long perframetime=0;
	long lasttime=0;
	int bNextIsKey=1;
	RTMP_LogLevel lvl=RTMP_LOGDEBUG;
	FILE*fp=NULL;	
	if (!InitSockets()){
		printf("Init Socket Err\n");
		return -1;
	}

	RTMP_LogSetLevel(lvl);

	rtmp=RTMP_Alloc();
	RTMP_Init(rtmp);
	rtmp->Link.timeout=5;//设置连接超时，单位秒，默认30秒

	packet=(RTMPPacket*)malloc(sizeof(RTMPPacket));
	memset(packet,0,sizeof(RTMPPacket));
	RTMPPacket_Alloc(packet,1024*64);
	RTMPPacket_Reset(packet);
	
	RTMP_SetupURL(rtmp,"rtmp://192.168.199.166/publishlive/livestream");
	//Important
	RTMP_EnableWrite(rtmp);

	if (!RTMP_Connect(rtmp,NULL)){
		printf("Connect Err\n");
		CleanupSockets();
		return -1;
	}
	//创建并发布流(取决于rtmp->Link.lFlags)
	if (!RTMP_ConnectStream(rtmp,0)){
		printf("ConnectStream Err\n");
		RTMP_Close(rtmp);
		CleanupSockets();
		return -1;
	}
	packet->m_hasAbsTimestamp = 0; //绝对时间戳
	packet->m_nChannel = 0x04; //通道
	packet->m_nInfoField2 = rtmp->m_stream_id;

	fp=fopen("cuc_ieschool.flv","rb");
	if (fp==NULL){
		printf("Open File Error.\n");
		RTMP_Close(rtmp);
		CleanupSockets();
		return -1;
	}

	printf("Start to send data ...\n");
	
	fseek(fp,9,SEEK_SET);//跳过前9个字节
	fseek(fp,4,SEEK_CUR);//跳过4字节长度
	start=time(NULL)-1;
	perframetime=0;//上一帧时间戳
	while(1){
		uint32_t type=0;//类型
		uint32_t datalength=0;//数据长度
		uint32_t timestamp=0;//时间戳
		uint32_t streamid=0;//流ID
		uint32_t alldatalength=0;//该帧总长度

		if(((time(NULL)-start)<(perframetime/1000))&&bNextIsKey){	
			//发的太快就等一下
			if(perframetime>lasttime){
				printf("TimeStamp:%8lu ms\n",perframetime);
				lasttime=perframetime;
			}
			Sleep(1000);
			continue;
		}	
		if(!ReadU8(&type,fp))
			break;
		if(!ReadU24(&datalength,fp))
			break;
		if(!ReadTime(&timestamp,fp))
			break;
		if(!ReadU24(&streamid,fp))
			break;
		if (type!=0x08&&type!=0x09){
			//跳过非音视频桢
			fseek(fp,datalength+4,SEEK_CUR);
			continue;
		}
		if(fread(packet->m_body,1,datalength,fp)!=datalength)
			break;
		packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM; 
		packet->m_nTimeStamp = timestamp; 
		packet->m_packetType=type;
		packet->m_nBodySize=datalength;

		if (!RTMP_IsConnected(rtmp)){
			printf("rtmp is not connect\n");
			break;
		}
		if (!RTMP_SendPacket(rtmp,packet,0)){
			printf("Send Error\n");
			break;
		}
		if(!ReadU32(&alldatalength,fp))
			break;
		perframetime=timestamp;
			
		bNextIsKey=0;
		if(!PeekU8(&type,fp))
			break;
		if(type==0x09){
			if(fseek(fp,11,SEEK_CUR)!=0)
				break;
			if(!PeekU8(&type,fp)){
				break;
			}
			if(type==0x17){
				bNextIsKey=1;
			}
			fseek(fp,-11,SEEK_CUR);
		}
	}
	printf("\nSend Data Over\n");
	fclose(fp);

	if (rtmp!=NULL){
		RTMP_Close(rtmp);//断开连接
		RTMP_Free(rtmp);//释放内存
		rtmp=NULL;
	}
	if (packet!=NULL){
		RTMPPacket_Free(packet);//释放内存
		free(packet);
		packet=NULL;
	}

	CleanupSockets();
	return 0;
}

int send_use_write(){
	return 0;
}

int main(int argc, char* argv[]){
	send_use_packet();
	send_use_write();
	return 0;
}
