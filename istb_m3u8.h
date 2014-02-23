#ifndef ISTB_M3U8_H
#define ISTB_M3U8_H


#ifdef __cplusplus
extern "C" {
#endif


#define M3U8_DEBUG

#ifdef M3U8_DEBUG
#define m3u8DBG(format,args...) \
do{ \
printf("m3u8DBG[%s:%d]:"format"",__FUNCTION__,__LINE__,##args);\
}while(0)
#else  
#define m3u8DBG(format,args...)  do{}while(0)
#endif

typedef enum
{
    M3U8_SEGMENT_READY   = 0,
    M3U8_SEGMENT_FINISH   = 1,
};

typedef int  (*READ_DATA_FUN) (void *opaque, uint8_t *buf, int buf_size);
typedef int  (*WRITE_DATA_FUN) (void *opaque, uint8_t *buf, int buf_size);
typedef int  (*M3U8_EVENT_CALLBACK)(int eventtype);

pthread_t istb_m3u8_start(char * optionstrings, READ_DATA_FUN read_data_fun);
int istb_m3u8_event_subscribe(M3U8_EVENT_CALLBACK Callback);
int istb_m3u8_event_report(int eventtype);
int istb_m3u8_stop(pthread_t tid);
int istb_m3u8_segment_get(char *filename,char * readbuf, int readlen);
#ifdef __cplusplus
}
#endif

#endif