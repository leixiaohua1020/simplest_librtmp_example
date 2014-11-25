/**
 * Simplest Librtmp Receive
 *
 * 雷霄骅，张晖
 * leixiaohua1020@126.com
 * zhanghuicuc@gmail.com
 * 中国传媒大学/数字电视技术
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * 本程序用于接收RTMP流媒体并在本地保存成FLV格式的文件。
 *
 */
#include <stdio.h>
#include "librtmp/rtmp_sys.h"
#include "librtmp/log.h"


int InitSockets()
{
	WORD version;
	WSADATA wsaData;
	version = MAKEWORD(1, 1);
	return (WSAStartup(version, &wsaData) == 0);
}

void CleanupSockets()
{
	WSACleanup();
}

int main(int argc, char* argv[])
{
	InitSockets();
	RTMP rtmp={0};
	RTMP_Init(&rtmp);
	rtmp.Link.timeout=25;//超时设置
	double duration=-1;
	int nRead;
	FILE *fp=fopen("receive.flv","wb");
	int buffsize=1024*1024*10;
	char *buff=(char*)malloc(buffsize);
	long countbuffsize=0;
	bool bLiveStream=true;//是否直播

	RTMP_SetupURL(&rtmp,"rtmp://192.168.199.166/publishlive/livestream");
	if (bLiveStream){
		rtmp.Link.lFlags|=RTMP_LF_LIVE;
	}

	RTMP_SetBufferMS(&rtmp, 3600*1000);//1hour
	if(!RTMP_Connect(&rtmp,NULL)){
		printf("Connect Server Error\n");
		WSACleanup();
		return -1;
	}
	if(!RTMP_ConnectStream(&rtmp,0)){
		printf("Connect Stream Error\n");
		RTMP_Close(&rtmp);
		WSACleanup();
		return -1;
	}

	while(nRead=RTMP_Read(&rtmp,buff,buffsize)){
		fwrite(buff,1,nRead,fp);
		if (!bLiveStream&&duration<0){
			duration = RTMP_GetDuration(&rtmp);
			printf("Duration:%f\n",duration);
		}
		countbuffsize+=nRead;
		printf("Receive: %dByte, Total: %0.2fkB\n",nRead,countbuffsize*1.0/1024);
	}

	fclose(fp);
	free(buff);
	buff=NULL;
	RTMP_Close(&rtmp);
	WSACleanup();
	return 0;
}