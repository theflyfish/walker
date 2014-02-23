#ifndef ISTB_RINGBUFFER_H
#define ISTB_RINGBUFFER_H


#ifdef __cplusplus
extern "C" {
#endif

#define RingDBG(format,args...) \
do{ \
printf("RingDBG[%s:%d]:"format"",__FUNCTION__,__LINE__,##args);\
}while(0)

typedef struct {
    pthread_mutex_t locker;
    char *buf;
    int buffersize;
    int write_ptr;
    int read_ptr;
} RingBuffer;

RingBuffer * iSTB_Buffer_create(int length);
void iSTB_Buffer_destroy(RingBuffer *ringbuffer);
int   iSTB_Buffer_write(RingBuffer *ringbuffer, char *data, int length);
int   iSTB_Buffer_read(RingBuffer *buffer, char *target, int amount);

#ifdef __cplusplus
}
#endif

#endif