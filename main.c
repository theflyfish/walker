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
#include <stdarg.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/types.h>
#include <signal.h>
#include <getopt.h>

//#include <unistd.h>
//#include "utils.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <semaphore.h>
#include "libav-compat.h"
#include "istb_ringbuffer.h"
#include "istb_httpserver.h"
#include "istb_m3u8.h"
#include "istb_mongoose.h"
#include "istb_udp.h"


static sem_t* g_HttpServer_Sem = NULL;
int myterminate=0;
static RingBuffer  *buffer_queue=NULL;

#define BF_RINGBUF_SIZE (188*1024*10*10*5)


int m3u8_read_data(void *opaque, uint8_t *buf, int buf_size) {
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

int udp_write_data(char  *buf, int len){
    //printf("%s:coming.... \n",__FUNCTION__);
    iSTB_Buffer_write(buffer_queue,buf,len);
    return 0;
}



int  M3u8_Event_Callback(int eventtype)
{
    printf("%s:m3u8 %d \n",__FUNCTION__,eventtype);
    if (eventtype == M3U8_SEGMENT_READY) {
        sem_post(g_HttpServer_Sem);
    }
    return 0; 
}
int  Udp_Event_Callback(int eventtype)
{
    printf("%s:udp %d \n",__FUNCTION__,eventtype);
    return 0; 
}

void main_handler(int signum) {
    (void)signum;
    myterminate = 1;
}
/*
-i:    input stream address  as: 127.0.0.1:1234 , used by udp modules
-o:  the output  address  for   hls segment streams,  used by  httpserver  modules, as: 192.168.1.15:8080
-n:  the maxnum of stream  segments  created by  m3u8 modules 
-d:  by second,the duration of every stream segment,used by m3u8 modules 
-p:  the prefix for segment name,such as: walk-1.ts\walk-2.ts\walk-3.ts, for the m3u8 index file 
-m: the file name of m3u8 file 
*/  
// ./istb-m3u8 -i 127.0.0.1:1234     -o  8909  -n 15 -d 10 -p istb -m yuxiang.m3u8  -u  http://127.0.0.1:8080/  
//  ./istb-m3u8 -i 127.0.0.1:1234     -o  8909  -n 15 -d 10 -p istb -m yuxiang.m3u8  
// ./istb-m3u8 -i 127.0.0.1:1234     -o  127.0.0.1:8909  -n 15 -d 10 -p istb -m yuxiang.m3u8  
//./iUDPtoHLS -i 127.0.0.1:1234     -o  127.0.0.1:8909  -n 5 -d 10 -p istb -m yuxiang.m3u8  
typedef struct{
    char * input_ip;
    int input_port;
    char *output_ip;
    int output_port;
    int m3u8_maxnum;
    int m3u8_duration;
    char *m3u8_prefix;
    char *m3u8_filename;
    char *m3u8_uri_prefix;
}io_options_t;

int  main(int argc,char *argv[])
{
    pthread_t httpserver_tid=0;
    pthread_t m3u8_tid=0;
    pthread_t udp_tid=0;
    struct sigaction act;
    int ch;
    char *p=NULL;
    io_options_t opts;
    char buf[128]={0};
   #if 1
    while ((ch = getopt(argc, argv, "i:o:n:d:p:m:h")) != -1)
    {
        switch (ch) {
        case 'i':
            printf("%c: %s\n", ch,optarg);
            /* strtok places a NULL terminator 
            in front of the token, if found */ 
            p = strtok(optarg, ":"); 
            if (p) {
                 printf("%s \n", p); 
                 opts.input_ip=strdup(p);
            }
            /* A second call to strtok using a NULL 
            as the first parameter returns a pointer 
            to the character following the token  */ 
            p = strtok(NULL, ":"); 
            if (p){
                printf("%s \n", p);
                opts.input_port=strtol(p,NULL,10);
            }
            break;
        case 'o':
            printf("%c :%s\n", ch,optarg);
            p = strtok(optarg, ":"); 
            if (p) {
                 printf("%s \n", p); 
                 opts.output_ip=strdup(p);
            }
            p = strtok(NULL, ":"); 
            if (p){
                printf("%s \n", p);
                opts.output_port=strtol(p,NULL,10);
            }
            break;
        case 'n':
            printf("%c :%s\n",ch, optarg);
            opts.m3u8_maxnum=strtol(optarg,NULL,10);
            break;
        case 'd':
            printf("%c: %s\n",ch, optarg);
            opts.m3u8_duration=strtol(optarg,NULL,10);
            break;
        case 'p':
            printf("%c :%s\n",ch, optarg);
            opts.m3u8_prefix=strdup(optarg);
            break;
        case 'm':
            printf("%c :%s\n",ch, optarg);
            opts.m3u8_filename=strdup(optarg);
            break;
        case 'h':
            printf("-h: yeah! you get the  secret! \n"
                    "-i:  input stream address  as: 127.0.0.1:1234 , used by udp modules \n"
                    "-o:  the output  address  for   hls segment streams,  used by  httpserver  modules, as: 192.168.1.15:8080 \n"
                    "-n:  the maxnum of stream  segments  created by  m3u8 modules \n"
                    "-d:  the duration of every stream segment,used by m3u8 modules \n" 
                    "-p:  the prefix for segment name,such as: walk-1.ts\walk-2.ts\walk-3.ts, for the m3u8 index file \n"
                   "-m:  the file name of m3u8 file \n");
            break;
        default:
            printf("unknown tag %c \n",ch);
            break;
        }
    }

    if (!opts.input_ip) {
        printf("%d:param error \n",__LINE__);
        return 1;
    }
    if (!opts.output_ip) {
        printf("%d:param error \n",__LINE__);
        return 1;
    }
    if (!opts.m3u8_filename) {
        printf("%d:param error \n",__LINE__);
        return 1;
    }
    if (!opts.m3u8_prefix) {
        printf("%d:param error \n",__LINE__);
        return 1;
    }

    snprintf(buf,sizeof(buf),"http://%s:%d/",opts.output_ip,opts.output_port);
    opts.m3u8_uri_prefix=strdup(buf);
    printf("inputip:%s:%d\n"
           "outputip:%s:%d\n"
           "m3u8filename:%s\n"
           "m3u8prefix:%s\n"
           "m3u8max:%d,durtion:%d\n"
           "m3u8_uri_prefix:%s\n",opts.input_ip,opts.input_port,opts.output_ip,opts.output_port,
           opts.m3u8_filename,opts.m3u8_prefix,opts.m3u8_maxnum,opts.m3u8_duration,
           opts.m3u8_uri_prefix);

    #endif
    #if 1
    g_HttpServer_Sem = malloc(sizeof(sem_t));
    sem_init(g_HttpServer_Sem, 0 ,0);

    buffer_queue=iSTB_Buffer_create(BF_RINGBUF_SIZE); 
    istb_udp_event_subscribe(Udp_Event_Callback);
    //udp_tid   =istb_udp_recv_start("127.0.0.1",1234,udp_write_data);
    udp_tid   =istb_udp_recv_start(opts.input_ip,opts.input_port,udp_write_data);
    istb_m3u8_event_subscribe(M3u8_Event_Callback);
    memset(buf,0,sizeof(buf));
    snprintf(buf,sizeof(buf),"-n %d -d %d -p %s -m %s -u %s",opts.m3u8_maxnum,opts.m3u8_duration,opts.m3u8_prefix,
             opts.m3u8_filename,opts.m3u8_uri_prefix);
    //m3u8_tid=istb_m3u8_start("-n 15 -d 10 -p tmp/istb -m tmp/yuxiang.m3u8 -u http://127.0.0.1:8080/",m3u8_read_data);
    m3u8_tid=istb_m3u8_start(buf,m3u8_read_data);
     sem_wait(g_HttpServer_Sem);
     mg_io_fun *http_io_opreate=calloc(1,sizeof(mg_io_fun));
     http_io_opreate->mg_io_open=istb_m3u8_api_open;
     http_io_opreate->mg_io_read=istb_m3u8_api_read;
     http_io_opreate->mg_io_write=istb_m3u8_api_write;
     http_io_opreate->mg_io_seek=istb_m3u8_api_seek;
     http_io_opreate->mg_io_close=istb_m3u8_api_close;
     //httpserver_tid=istb_httpserver_create(8080,".",http_io_opreate);
    memset(buf,0,sizeof(buf));
    snprintf(buf,sizeof(buf),"%s:%d",opts.output_ip,opts.output_port);
    httpserver_tid=istb_httpserver_create(buf,".",http_io_opreate);
#if 0
    printf("%s:sleep 30s \n",__FUNCTION__);
    usleep(1000*1000*20);//5 min
    printf("%s:now destory \n",__FUNCTION__);
    myterminate=1;
#endif
     printf("%s now waiting...\n",__FUNCTION__);
     act.sa_handler = &main_handler;

     sigaction(SIGINT, &act, NULL);
     sigaction(SIGTERM, &act, NULL);
     while (!myterminate) {
         usleep(1000*1000);
     }
    //destory the thread process ,anf free memory buffer
    istb_m3u8_stop(m3u8_tid); 
    istb_udp_recv_destroy(udp_tid);
    istb_httpserver_destroy(httpserver_tid);
    iSTB_Buffer_destroy(buffer_queue);
    free(opts.input_ip);opts.input_ip=NULL;
    free(opts.output_ip);opts.output_ip=NULL;
    free(opts.m3u8_filename);opts.m3u8_filename=NULL;
    free(opts.m3u8_prefix);opts.m3u8_prefix=NULL;
    free(opts.m3u8_uri_prefix);opts.m3u8_uri_prefix=NULL;
    #endif
    return 0;
}

