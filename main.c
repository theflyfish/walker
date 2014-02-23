/*
 * Copyright (c) 2009 Chase Douglas
 * Copyright (c) 2011 John Ferlito
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <getopt.h>
#include <pthread.h>
//#include "utils.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libav-compat.h"
#include "istb_ringbuffer.h"
#include "istb_httpserver.h"
#include "istb_m3u8.h"


#define BF_RINGBUF_SIZE (188*1024*10*10*5)
#define FILE_SERVER_BUFF_SIZE (188*10)
#define VLC_Test_File_Path  "/home/yuxiang/workspace/m3u8-test/src/test.ts"
static int isNeedShutDown=0;
static RingBuffer * buffer_queue=NULL;
void Buffer_FileServer_Task(void)
{
    char ReadBuf_file[FILE_SERVER_BUFF_SIZE]={0};
    FILE * fp_VlCTest=NULL;
    int have_read=0;
    long int file_size=0;
    long int file_pos=0;
    //int have_send=0;
    printf("coming..\n");
    fp_VlCTest = fopen(VLC_Test_File_Path, "rb+" );
    if(fp_VlCTest == NULL)
    {
        printf("can not open file!\n");
        return ;
    }
    fseek(fp_VlCTest,0,SEEK_END); 
    file_size = ftell(fp_VlCTest);
    //fseek(fp_VlCTest,0,SEEK_SET); 
    rewind(fp_VlCTest);
    printf(":xxxfile_size:%ld,fp:%p \n",file_size,fp_VlCTest);
    do{ 
        if (isNeedShutDown)
        {
            printf("stop file server \n");
            break;
        }
        have_read = fread(ReadBuf_file, sizeof(char), FILE_SERVER_BUFF_SIZE, fp_VlCTest); 
        if (have_read!=FILE_SERVER_BUFF_SIZE)
        {
            if ((file_pos+have_read)==file_size)
            {
                printf("read file get end!\n"); 
            }
            else
            {
                printf("read file error!\n"); 
                break;
            }
        }
        file_pos += have_read;
        //printf(" send live streamer \n");
        iSTB_Buffer_write(buffer_queue,ReadBuf_file,FILE_SERVER_BUFF_SIZE);
        //printf("%s:have send=%d \n",__FUNCTION__,have_send);
        //usleep(100);//0.1ms
        usleep(1000*10);//10ms

    }while(file_pos < file_size);
    fclose(fp_VlCTest);
    printf("file streamer push finished !\n");
    usleep(1000*1000);//1s
    return; 
}

int Buffer_FileServer_Start(void)
{
    int s=0;
    pthread_attr_t attr;
    pthread_t      ThreadId;
   // int ret =0;



    s = pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    
    if(pthread_create (&ThreadId, &attr, (void *(*)(void *))Buffer_FileServer_Task, NULL))
    {
        printf("create http server thread error \n");
        pthread_attr_destroy(&attr);
        return -1;
    }
    s = pthread_attr_destroy(&attr);
    return 0;
}

int myterminate=0;
FILE* file=NULL;
int read_data(void *opaque, uint8_t *buf, int buf_size) {
//	UdpParam udphead;
	int size = buf_size;
	int ret;
    int filled=0;
	//printf("opaque:%p \n", opaque);
	do {
        if (myterminate){
            break;
        }
        ret = iSTB_Buffer_read(buffer_queue,(char *)buf + filled, size); 
        filled+=ret;
        size-=ret;
	} while (size);
	//printf("read data Ok %d\n", filled);
	return filled;
}
pthread_t httpserver_tid=0;
int  M3u8_Event_Callback(int eventtype)
{
    printf("laalal %d \n",eventtype);
    if (eventtype == M3U8_SEGMENT_READY) {
        httpserver_tid=istb_httpserver_create(8080,".");
    }
    return 0; 
}


int  main(void)
{
    pthread_t m3u8_tid=0;
    #if 1
    #endif
    #if 1
    buffer_queue=iSTB_Buffer_create(BF_RINGBUF_SIZE); 
    Buffer_FileServer_Start();
    istb_m3u8_event_subscribe(M3u8_Event_Callback);
    m3u8_tid=istb_m3u8_start("-n 2 -d 10 -p tmp/istb -m tmp/yuxiang.m3u8 -u http://127.0.0.1:8080/",read_data);
    printf("%s:sleep 30s \n",__FUNCTION__);
    usleep(1000*1000*30);//5 min
    printf("%s:now destory \n",__FUNCTION__);
    myterminate=1;
    istb_m3u8_stop(m3u8_tid);
    istb_httpserver_destroy(httpserver_tid);
    iSTB_Buffer_destroy(buffer_queue);
    #endif
    return 0;
}


