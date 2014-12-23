/**
 * Simplest Librtmp Send 264
 *
 * 雷霄骅，张晖
 * leixiaohua1020@126.com
 * zhanghuicuc@gmail.com
 * 中国传媒大学/数字电视技术
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * 本程序用于将内存中的H.264数据推送至RTMP流媒体服务器。
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "librtmp_send264.h"
#include "librtmp\rtmp.h"   
#include "librtmp\rtmp_sys.h"   
#include "librtmp\amf.h"  
#include "sps_decode.h"

#ifdef WIN32     
#include <windows.h>  
#pragma comment(lib,"WS2_32.lib")   
#pragma comment(lib,"winmm.lib")  
#endif 

//定义包头长度，RTMP_MAX_HEADER_SIZE=18
#define RTMP_HEAD_SIZE   (sizeof(RTMPPacket)+RTMP_MAX_HEADER_SIZE)
//存储Nal单元数据的buffer大小
#define BUFFER_SIZE 32768
//搜寻Nal单元时的一些标志
#define GOT_A_NAL_CROSS_BUFFER BUFFER_SIZE+1
#define GOT_A_NAL_INCLUDE_A_BUFFER BUFFER_SIZE+2
#define NO_MORE_BUFFER_TO_READ BUFFER_SIZE+3

/**
 * _NaluUnit
 * 内部结构体。该结构体主要用于存储和传递Nal单元的类型、大小和数据
 */ 
typedef struct _NaluUnit  
{  
	int type;  
    int size;  
	unsigned char *data;  
}NaluUnit;

/**
 * _RTMPMetadata
 * 内部结构体。该结构体主要用于存储和传递元数据信息
 */ 
typedef struct _RTMPMetadata  
{  
	// video, must be h264 type   
	unsigned int    nWidth;  
	unsigned int    nHeight;  
	unsigned int    nFrameRate;      
	unsigned int    nSpsLen;  
	unsigned char   *Sps;  
	unsigned int    nPpsLen;  
	unsigned char   *Pps;   
} RTMPMetadata,*LPRTMPMetadata;  

enum  
{  
	 VIDEO_CODECID_H264 = 7,  
};  

/**
 * 初始化winsock
 *					
 * @成功则返回1 , 失败则返回相应错误代码
 */ 
int InitSockets()    
{    
	#ifdef WIN32     
		WORD version;    
		WSADATA wsaData;    
		version = MAKEWORD(1, 1);    
		return (WSAStartup(version, &wsaData) == 0);    
	#else     
		return TRUE;    
	#endif     
}

/**
 * 释放winsock
 *					
 * @成功则返回0 , 失败则返回相应错误代码
 */ 
inline void CleanupSockets()    
{    
	#ifdef WIN32     
		WSACleanup();    
	#endif     
}    

//网络字节序转换
char * put_byte( char *output, uint8_t nVal )    
{    
	output[0] = nVal;    
	return output+1;    
}   

char * put_be16(char *output, uint16_t nVal )    
{    
	output[1] = nVal & 0xff;    
	output[0] = nVal >> 8;    
	return output+2;    
}  

char * put_be24(char *output,uint32_t nVal )    
{    
	output[2] = nVal & 0xff;    
	output[1] = nVal >> 8;    
	output[0] = nVal >> 16;    
	return output+3;    
}    
char * put_be32(char *output, uint32_t nVal )    
{    
	output[3] = nVal & 0xff;    
	output[2] = nVal >> 8;    
	output[1] = nVal >> 16;    
	output[0] = nVal >> 24;    
	return output+4;    
}    
char *  put_be64( char *output, uint64_t nVal )    
{    
	output=put_be32( output, nVal >> 32 );    
	output=put_be32( output, nVal );    
	return output;    
}  

char * put_amf_string( char *c, const char *str )    
{    
	uint16_t len = strlen( str );    
	c=put_be16( c, len );    
	memcpy(c,str,len);    
	return c+len;    
}    
char * put_amf_double( char *c, double d )    
{    
	*c++ = AMF_NUMBER;  /* type: Number */    
	{    
		unsigned char *ci, *co;    
		ci = (unsigned char *)&d;    
		co = (unsigned char *)c;    
		co[0] = ci[7];    
		co[1] = ci[6];    
		co[2] = ci[5];    
		co[3] = ci[4];    
		co[4] = ci[3];    
		co[5] = ci[2];    
		co[6] = ci[1];    
		co[7] = ci[0];    
	}    
	return c+8;    
}  


unsigned int  m_nFileBufSize; 
unsigned int  nalhead_pos;
RTMP* m_pRtmp;  
RTMPMetadata metaData;
unsigned char *m_pFileBuf;  
unsigned char *m_pFileBuf_tmp;
unsigned char* m_pFileBuf_tmp_old;	//used for realloc

/**
 * 初始化并连接到服务器
 *
 * @param url 服务器上对应webapp的地址
 *					
 * @成功则返回1 , 失败则返回0
 */ 
int RTMP264_Connect(const char* url)  
{  
	nalhead_pos=0;
	m_nFileBufSize=BUFFER_SIZE;
	m_pFileBuf=(unsigned char*)malloc(BUFFER_SIZE);
	m_pFileBuf_tmp=(unsigned char*)malloc(BUFFER_SIZE);
	InitSockets();  

	m_pRtmp = RTMP_Alloc();
	RTMP_Init(m_pRtmp);
	/*设置URL*/
	if (RTMP_SetupURL(m_pRtmp,(char*)url) == FALSE)
	{
		RTMP_Free(m_pRtmp);
		return false;
	}
	/*设置可写,即发布流,这个函数必须在连接前使用,否则无效*/
	RTMP_EnableWrite(m_pRtmp);
	/*连接服务器*/
	if (RTMP_Connect(m_pRtmp, NULL) == FALSE) 
	{
		RTMP_Free(m_pRtmp);
		return false;
	} 

	/*连接流*/
	if (RTMP_ConnectStream(m_pRtmp,0) == FALSE)
	{
		RTMP_Close(m_pRtmp);
		RTMP_Free(m_pRtmp);
		return false;
	}
	return true;  
}  


/**
 * 断开连接，释放相关的资源。
 *
 */    
void RTMP264_Close()  
{  
	if(m_pRtmp)  
	{  
		RTMP_Close(m_pRtmp);  
		RTMP_Free(m_pRtmp);  
		m_pRtmp = NULL;  
	}  
	CleanupSockets();   
	if (m_pFileBuf != NULL)
	{  
		free(m_pFileBuf);
	}  
	if (m_pFileBuf_tmp != NULL)
	{  
		free(m_pFileBuf_tmp);
	}
} 

/**
 * 发送RTMP数据包
 *
 * @param nPacketType 数据类型
 * @param data 存储数据内容
 * @param size 数据大小
 * @param nTimestamp 当前包的时间戳
 *
 * @成功则返回 1 , 失败则返回一个小于0的数
 */
int SendPacket(unsigned int nPacketType,unsigned char *data,unsigned int size,unsigned int nTimestamp)  
{  
	RTMPPacket* packet;
	/*分配包内存和初始化,len为包体长度*/
	packet = (RTMPPacket *)malloc(RTMP_HEAD_SIZE+size);
	memset(packet,0,RTMP_HEAD_SIZE);
	/*包体内存*/
	packet->m_body = (char *)packet + RTMP_HEAD_SIZE;
	packet->m_nBodySize = size;
	memcpy(packet->m_body,data,size);
	packet->m_hasAbsTimestamp = 0;
	packet->m_packetType = nPacketType; /*此处为类型有两种一种是音频,一种是视频*/
	packet->m_nInfoField2 = m_pRtmp->m_stream_id;
	packet->m_nChannel = 0x04;

	packet->m_headerType = RTMP_PACKET_SIZE_LARGE;
	if (RTMP_PACKET_TYPE_AUDIO ==nPacketType && size !=4)
	{
		packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
	}
	packet->m_nTimeStamp = nTimestamp;
	/*发送*/
	int nRet =0;
	if (RTMP_IsConnected(m_pRtmp))
	{
		nRet = RTMP_SendPacket(m_pRtmp,packet,TRUE); /*TRUE为放进发送队列,FALSE是不放进发送队列,直接发送*/
	}
	/*释放内存*/
	free(packet);
	return nRet;  
}  

/**
 * 发送视频的sps和pps信息
 *
 * @param pps 存储视频的pps信息
 * @param pps_len 视频的pps信息长度
 * @param sps 存储视频的pps信息
 * @param sps_len 视频的sps信息长度
 *
 * @成功则返回 1 , 失败则返回0
 */
int SendVideoSpsPps(unsigned char *pps,int pps_len,unsigned char * sps,int sps_len)
{
	RTMPPacket * packet=NULL;//rtmp包结构
	unsigned char * body=NULL;
	int i;
	packet = (RTMPPacket *)malloc(RTMP_HEAD_SIZE+1024);
	//RTMPPacket_Reset(packet);//重置packet状态
	memset(packet,0,RTMP_HEAD_SIZE+1024);
	packet->m_body = (char *)packet + RTMP_HEAD_SIZE;
	body = (unsigned char *)packet->m_body;
	i = 0;
	body[i++] = 0x17;
	body[i++] = 0x00;

	body[i++] = 0x00;
	body[i++] = 0x00;
	body[i++] = 0x00;

	/*AVCDecoderConfigurationRecord*/
	body[i++] = 0x01;
	body[i++] = sps[1];
	body[i++] = sps[2];
	body[i++] = sps[3];
	body[i++] = 0xff;

	/*sps*/
	body[i++]   = 0xe1;
	body[i++] = (sps_len >> 8) & 0xff;
	body[i++] = sps_len & 0xff;
	memcpy(&body[i],sps,sps_len);
	i +=  sps_len;

	/*pps*/
	body[i++]   = 0x01;
	body[i++] = (pps_len >> 8) & 0xff;
	body[i++] = (pps_len) & 0xff;
	memcpy(&body[i],pps,pps_len);
	i +=  pps_len;

	packet->m_packetType = RTMP_PACKET_TYPE_VIDEO;
	packet->m_nBodySize = i;
	packet->m_nChannel = 0x04;
	packet->m_nTimeStamp = 0;
	packet->m_hasAbsTimestamp = 0;
	packet->m_headerType = RTMP_PACKET_SIZE_MEDIUM;
	packet->m_nInfoField2 = m_pRtmp->m_stream_id;

	/*调用发送接口*/
	int nRet = RTMP_SendPacket(m_pRtmp,packet,TRUE);
	free(packet);    //释放内存
	return nRet;
}

/**
 * 发送H264数据帧
 *
 * @param data 存储数据帧内容
 * @param size 数据帧的大小
 * @param bIsKeyFrame 记录该帧是否为关键帧
 * @param nTimeStamp 当前帧的时间戳
 *
 * @成功则返回 1 , 失败则返回0
 */
int SendH264Packet(unsigned char *data,unsigned int size,int bIsKeyFrame,unsigned int nTimeStamp)  
{  
	if(data == NULL && size<11){  
		return false;  
	}  

	unsigned char *body = (unsigned char*)malloc(size+9);  
	memset(body,0,size+9);

	int i = 0; 
	if(bIsKeyFrame){  
		body[i++] = 0x17;// 1:Iframe  7:AVC   
		body[i++] = 0x01;// AVC NALU   
		body[i++] = 0x00;  
		body[i++] = 0x00;  
		body[i++] = 0x00;  


		// NALU size   
		body[i++] = size>>24 &0xff;  
		body[i++] = size>>16 &0xff;  
		body[i++] = size>>8 &0xff;  
		body[i++] = size&0xff;
		// NALU data   
		memcpy(&body[i],data,size);  
		SendVideoSpsPps(metaData.Pps,metaData.nPpsLen,metaData.Sps,metaData.nSpsLen);
	}else{  
		body[i++] = 0x27;// 2:Pframe  7:AVC   
		body[i++] = 0x01;// AVC NALU   
		body[i++] = 0x00;  
		body[i++] = 0x00;  
		body[i++] = 0x00;  


		// NALU size   
		body[i++] = size>>24 &0xff;  
		body[i++] = size>>16 &0xff;  
		body[i++] = size>>8 &0xff;  
		body[i++] = size&0xff;
		// NALU data   
		memcpy(&body[i],data,size);  
	}  
	

	int bRet = SendPacket(RTMP_PACKET_TYPE_VIDEO,body,i+size,nTimeStamp);  

	free(body);  

	return bRet;  
} 

/**
 * 从内存中读取出第一个Nal单元
 *
 * @param nalu 存储nalu数据
 * @param read_buffer 回调函数，当数据不足的时候，系统会自动调用该函数获取输入数据。
 *					2个参数功能：
 *					uint8_t *buf：外部数据送至该地址
 *					int buf_size：外部数据大小
 *					返回值：成功读取的内存大小
 * @成功则返回 1 , 失败则返回0
 */
int ReadFirstNaluFromBuf(NaluUnit &nalu,int (*read_buffer)(uint8_t *buf, int buf_size)) 
{
	int naltail_pos=nalhead_pos;
	memset(m_pFileBuf_tmp,0,BUFFER_SIZE);
	while(nalhead_pos<m_nFileBufSize)  
	{  
		//search for nal header
		if(m_pFileBuf[nalhead_pos++] == 0x00 && 
			m_pFileBuf[nalhead_pos++] == 0x00) 
		{
			if(m_pFileBuf[nalhead_pos++] == 0x01)
				goto gotnal_head;
			else 
			{
				//cuz we have done an i++ before,so we need to roll back now
				nalhead_pos--;		
				if(m_pFileBuf[nalhead_pos++] == 0x00 && 
					m_pFileBuf[nalhead_pos++] == 0x01)
					goto gotnal_head;
				else
					continue;
			}
		}
		else 
			continue;

		//search for nal tail which is also the head of next nal
gotnal_head:
		//normal case:the whole nal is in this m_pFileBuf
		naltail_pos = nalhead_pos;  
		while (naltail_pos<m_nFileBufSize)  
		{  
			if(m_pFileBuf[naltail_pos++] == 0x00 && 
				m_pFileBuf[naltail_pos++] == 0x00 )
			{  
				if(m_pFileBuf[naltail_pos++] == 0x01)
				{
					nalu.size = (naltail_pos-3)-nalhead_pos;
					break;
				}
				else
				{
					naltail_pos--;
					if(m_pFileBuf[naltail_pos++] == 0x00 &&
						m_pFileBuf[naltail_pos++] == 0x01)
					{	
						nalu.size = (naltail_pos-4)-nalhead_pos;
						break;
					}
				}
			}  
		}

		nalu.type = m_pFileBuf[nalhead_pos]&0x1f; 
		memcpy(m_pFileBuf_tmp,m_pFileBuf+nalhead_pos,nalu.size);
		nalu.data=m_pFileBuf_tmp;
		nalhead_pos=naltail_pos;
		return TRUE;   		
	}
}

/**
 * 从内存中读取出一个Nal单元
 *
 * @param nalu 存储nalu数据
 * @param read_buffer 回调函数，当数据不足的时候，系统会自动调用该函数获取输入数据。
 *					2个参数功能：
 *					uint8_t *buf：外部数据送至该地址
 *					int buf_size：外部数据大小
 *					返回值：成功读取的内存大小
 * @成功则返回 1 , 失败则返回0
 */
int ReadOneNaluFromBuf(NaluUnit &nalu,int (*read_buffer)(uint8_t *buf, int buf_size))  
{    
	
	int naltail_pos=nalhead_pos;
	int ret;
	int nalustart;//nal的开始标识符是几个00
	memset(m_pFileBuf_tmp,0,BUFFER_SIZE);
	nalu.size=0;
	while(1)
	{
		if(nalhead_pos==NO_MORE_BUFFER_TO_READ)
			return FALSE;
		while(naltail_pos<m_nFileBufSize)  
		{  
			//search for nal tail
			if(m_pFileBuf[naltail_pos++] == 0x00 && 
				m_pFileBuf[naltail_pos++] == 0x00) 
			{
				if(m_pFileBuf[naltail_pos++] == 0x01)
				{	
					nalustart=3;
					goto gotnal ;
				}
				else 
				{
					//cuz we have done an i++ before,so we need to roll back now
					naltail_pos--;		
					if(m_pFileBuf[naltail_pos++] == 0x00 && 
						m_pFileBuf[naltail_pos++] == 0x01)
					{
						nalustart=4;
						goto gotnal;
					}
					else
						continue;
				}
			}
			else 
				continue;

			gotnal:	
 				/**
				 *special case1:parts of the nal lies in a m_pFileBuf and we have to read from buffer 
				 *again to get the rest part of this nal
				 */
				if(nalhead_pos==GOT_A_NAL_CROSS_BUFFER || nalhead_pos==GOT_A_NAL_INCLUDE_A_BUFFER)
				{
					nalu.size = nalu.size+naltail_pos-nalustart;
					if(nalu.size>BUFFER_SIZE)
					{
						m_pFileBuf_tmp_old=m_pFileBuf_tmp;	//// save pointer in case realloc fails
						if((m_pFileBuf_tmp = (unsigned char*)realloc(m_pFileBuf_tmp,nalu.size)) ==  NULL )
						{
							free( m_pFileBuf_tmp_old );  // free original block
							return FALSE;
						}
					}
					memcpy(m_pFileBuf_tmp+nalu.size+nalustart-naltail_pos,m_pFileBuf,naltail_pos-nalustart);
					nalu.data=m_pFileBuf_tmp;
					nalhead_pos=naltail_pos;
					return TRUE;
				}
				//normal case:the whole nal is in this m_pFileBuf
				else 
				{  
					nalu.type = m_pFileBuf[nalhead_pos]&0x1f; 
					nalu.size=naltail_pos-nalhead_pos-nalustart;
					if(nalu.type==0x06)
					{
						nalhead_pos=naltail_pos;
						continue;
					}
					memcpy(m_pFileBuf_tmp,m_pFileBuf+nalhead_pos,nalu.size);
					nalu.data=m_pFileBuf_tmp;
					nalhead_pos=naltail_pos;
					return TRUE;    
				} 					
		}

		if(naltail_pos>=m_nFileBufSize && nalhead_pos!=GOT_A_NAL_CROSS_BUFFER && nalhead_pos != GOT_A_NAL_INCLUDE_A_BUFFER)
		{
			nalu.size = BUFFER_SIZE-nalhead_pos;
			nalu.type = m_pFileBuf[nalhead_pos]&0x1f; 
			memcpy(m_pFileBuf_tmp,m_pFileBuf+nalhead_pos,nalu.size);
			if((ret=read_buffer(m_pFileBuf,m_nFileBufSize))<BUFFER_SIZE)
			{
				memcpy(m_pFileBuf_tmp+nalu.size,m_pFileBuf,ret);
				nalu.size=nalu.size+ret;
				nalu.data=m_pFileBuf_tmp;
				nalhead_pos=NO_MORE_BUFFER_TO_READ;
				return FALSE;
			}
			naltail_pos=0;
			nalhead_pos=GOT_A_NAL_CROSS_BUFFER;
			continue;
		}
		if(nalhead_pos==GOT_A_NAL_CROSS_BUFFER || nalhead_pos == GOT_A_NAL_INCLUDE_A_BUFFER)
		{
			nalu.size = BUFFER_SIZE+nalu.size;
				
				m_pFileBuf_tmp_old=m_pFileBuf_tmp;	//// save pointer in case realloc fails
				if((m_pFileBuf_tmp = (unsigned char*)realloc(m_pFileBuf_tmp,nalu.size)) ==  NULL )
				{
					free( m_pFileBuf_tmp_old );  // free original block
					return FALSE;
				}

			memcpy(m_pFileBuf_tmp+nalu.size-BUFFER_SIZE,m_pFileBuf,BUFFER_SIZE);
			
			if((ret=read_buffer(m_pFileBuf,m_nFileBufSize))<BUFFER_SIZE)
			{
				memcpy(m_pFileBuf_tmp+nalu.size,m_pFileBuf,ret);
				nalu.size=nalu.size+ret;
				nalu.data=m_pFileBuf_tmp;
				nalhead_pos=NO_MORE_BUFFER_TO_READ;
				return FALSE;
			}
			naltail_pos=0;
			nalhead_pos=GOT_A_NAL_INCLUDE_A_BUFFER;
			continue;
		}
	}
	return FALSE;  
} 

/**
 * 将内存中的一段H.264编码的视频数据利用RTMP协议发送到服务器
 *
 * @param read_buffer 回调函数，当数据不足的时候，系统会自动调用该函数获取输入数据。
 *					2个参数功能：
 *					uint8_t *buf：外部数据送至该地址
 *					int buf_size：外部数据大小
 *					返回值：成功读取的内存大小
 * @成功则返回1 , 失败则返回0
 */ 
int RTMP264_Send(int (*read_buffer)(unsigned char *buf, int buf_size))  
{    
	int ret;
	uint32_t now,last_update;
	  
	memset(&metaData,0,sizeof(RTMPMetadata));
	memset(m_pFileBuf,0,BUFFER_SIZE);
	if((ret=read_buffer(m_pFileBuf,m_nFileBufSize))<0)
	{
		return FALSE;
	}

	NaluUnit naluUnit;  
	// 读取SPS帧   
	ReadFirstNaluFromBuf(naluUnit,read_buffer);  
	metaData.nSpsLen = naluUnit.size;  
	metaData.Sps=NULL;
	metaData.Sps=(unsigned char*)malloc(naluUnit.size);
	memcpy(metaData.Sps,naluUnit.data,naluUnit.size);

	// 读取PPS帧   
	ReadOneNaluFromBuf(naluUnit,read_buffer);  
	metaData.nPpsLen = naluUnit.size; 
	metaData.Pps=NULL;
	metaData.Pps=(unsigned char*)malloc(naluUnit.size);
	memcpy(metaData.Pps,naluUnit.data,naluUnit.size);
	
	// 解码SPS,获取视频图像宽、高信息   
	int width = 0,height = 0, fps=0;  
	h264_decode_sps(metaData.Sps,metaData.nSpsLen,width,height,fps);  
	//metaData.nWidth = width;  
	//metaData.nHeight = height;  
	if(fps)
		metaData.nFrameRate = fps; 
	else
		metaData.nFrameRate = 25;

	//发送PPS,SPS
	//ret=SendVideoSpsPps(metaData.Pps,metaData.nPpsLen,metaData.Sps,metaData.nSpsLen);
	//if(ret!=1)
	//	return FALSE;

	unsigned int tick = 0;  
	unsigned int tick_gap = 1000/metaData.nFrameRate; 
	ReadOneNaluFromBuf(naluUnit,read_buffer);
	int bKeyframe  = (naluUnit.type == 0x05) ? TRUE : FALSE;
	while(SendH264Packet(naluUnit.data,naluUnit.size,bKeyframe,tick))  
	{    
got_sps_pps:
		//if(naluUnit.size==8581)
			printf("NALU size:%8d\n",naluUnit.size);
		last_update=RTMP_GetTime();
		if(!ReadOneNaluFromBuf(naluUnit,read_buffer))
				goto end;
		if(naluUnit.type == 0x07 || naluUnit.type == 0x08)
			goto got_sps_pps;
		bKeyframe  = (naluUnit.type == 0x05) ? TRUE : FALSE;
		tick +=tick_gap;
		now=RTMP_GetTime();
		msleep(tick_gap-now+last_update);  
		//msleep(40);
	}  
	end:
	free(metaData.Sps);
	free(metaData.Pps);
	return TRUE;  
}  


