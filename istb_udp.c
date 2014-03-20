#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#include "istb_udp.h"

struct udppara   
{   
    char *serverip;
    int  port;  
    UDP_WRITE_DATA udp_write_fun;   
};  

int connect_live=1;
static UDP_EVENT_CALLBACK udp_event_callback=NULL;


int istb_udp_event_report(int eventtype)
{
    if (udp_event_callback) {
        udpDBG("coming....\n");
        udp_event_callback(eventtype);
    }
    return 0;
}

int istb_udp_event_subscribe(UDP_EVENT_CALLBACK Callback)
{
    if (Callback) {
        udpDBG("coming....\n");
        udp_event_callback = Callback; 
    }
    return 0;
}




void* istb_udp_recv_task(void *arg) {
	struct sockaddr_in s_addr;
	struct sockaddr_in c_addr;
	int  udp_sock=-1;
	socklen_t addr_len;
	int len;
    int firstarrive=0;
	uint8_t buf[4096];
    int port=0;
    char serverip_str[128]={0};
    UDP_WRITE_DATA  udp_data_write;
    fd_set read_fds;
    int ready_fds;
    struct timeval tv;
    if (!arg) {
        udpDBG("param error ! \n");
        return 0;
    }
    struct udppara *para; 
    para=(struct udppara *)arg;

    port=para->port;
    strncpy(serverip_str,para->serverip,sizeof(serverip_str));
    udp_data_write =para->udp_write_fun;
        udpDBG("%d ,%s \n",port,serverip_str);
	if ( (udp_sock = socket(AF_INET, SOCK_DGRAM, 0))  == -1) {
		perror("socket");
		exit(errno);
	} else
		printf("create socket.\n\r");

	memset(&s_addr, 0, sizeof(struct sockaddr_in));
	s_addr.sin_family = AF_INET;
	if (port)
		s_addr.sin_port = htons(port);
	else
		s_addr.sin_port = htons(7838);


	s_addr.sin_addr.s_addr = INADDR_ANY;

	if ( (bind(udp_sock, (struct sockaddr*)&s_addr, sizeof(s_addr))) == -1 ) {
		perror("bind");
		exit(errno);
	}else{
		printf("bind address to socket.\n\r");
    }
	addr_len = sizeof(struct sockaddr);


	while(connect_live) {
        FD_ZERO(&read_fds);
        FD_SET(udp_sock, &read_fds);
        tv.tv_sec  = 10;//10s
        tv.tv_usec = 0;
        ready_fds = select(udp_sock + 1, &read_fds, NULL, NULL,&tv);
        if (ready_fds< 0) {
            udpDBG("error\n");
            connect_live=0;
            continue;
        } else if (ready_fds == 0) {
            udpDBG("time out \n");
            continue;
        }else if(udp_sock != -1 && FD_ISSET(udp_sock, &read_fds)){
            if (!firstarrive) {
                udp_event_callback(UDP_DATA_ARRIVE);
                firstarrive=1;
            }
            len = recvfrom(udp_sock, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&c_addr, &addr_len); 
            if (len < 0) {
                perror("recvfrom");
                connect_live =0;
                continue;
            }
            udp_data_write(buf,len);
        }else{
            udpDBG("what \n");
        }

	}
    FD_ZERO(&read_fds);
    close(udp_sock);
    udp_sock=-1;
    free(arg);
	return 0;
}



pthread_t istb_udp_recv_start( char * serverip, int port ,UDP_WRITE_DATA udp_write_fun){
    int s=0;
    pthread_attr_t attr;
    pthread_t      ThreadId;
    //int ret =0;
    struct udppara *para=NULL;
    if(!(para=(struct udppara *)calloc(1,sizeof(struct udppara)))){
        return 0;
    }
    para->port=port;
    para->serverip=serverip;
    para->udp_write_fun=udp_write_fun;

    s = pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    udpDBG("server:%s  %d \n",para->serverip,para->port);
    if(pthread_create (&ThreadId, &attr, (void *(*)(void *))istb_udp_recv_task, (void *)para))
    {
        udpDBG("create http server thread error \n");
        pthread_attr_destroy(&attr);
        return 0;
    }
    udpDBG("ThreadId:%ld \n",ThreadId);
    s = pthread_attr_destroy(&attr);
    return ThreadId;
}

int istb_udp_recv_destroy(pthread_t tid)
{
    int res=0;
    res = pthread_kill(tid, 0); //test is the pthread  alive
    udpDBG("thread  live status: %d \n",res);
    if (res ==3) {//ESRCH=3
        udpDBG("thread  has exsit!\n");
        return 0;
    }
    connect_live = 0; 
    while (1) {
        res = pthread_kill(tid, 0); //test is the pthread  alive
        if (res ==3) {//ESRCH=3
            break;
        }
        usleep(1000*10);//10ms
    }
    udpDBG("thread  stop finish \n");
    connect_live=1; //reset teminate flag
    return 0;
}




