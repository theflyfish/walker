#ifndef ISTB_UDP_H
#define ISTB_UDP_H


#ifdef __cplusplus
extern "C" {
#endif


#define UDP_DEBUG

#ifdef UDP_DEBUG
#define udpDBG(format,args...) \
do{ \
printf("udpDBG[%s:%d]:"format"",__FUNCTION__,__LINE__,##args);\
}while(0)
#else  
#define udpDBG(format,args...)  do{}while(0)
#endif

typedef enum
{
    UDP_DATA_ARRIVE   = 0,
};


typedef int  (*UDP_WRITE_DATA) (char *buf, int buf_size);
typedef int  (*UDP_EVENT_CALLBACK)(int eventtype);

int           istb_udp_event_subscribe(UDP_EVENT_CALLBACK Callback);
pthread_t istb_udp_recv_start( char * serverip, int port ,UDP_WRITE_DATA udp_write_fun);
int           istb_udp_recv_destroy(pthread_t tid);
#ifdef __cplusplus
}
#endif

#endif
