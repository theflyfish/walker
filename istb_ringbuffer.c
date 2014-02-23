
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<semaphore.h>

#include "istb_ringbuffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define  Ring_NullPtrCheck(p) \
    do {\
        if (NULL == (p))\
        {\
            RingDBG("param error \n");\
            return -1; \
        } \
    } while(0)

#define iSTB_Buffer_empty(BUF) (BUF->write_ptr=BUF->read_ptr=0)

#define iSTB_Buffer_read_at(BUF) ((BUF)->buf + (BUF)->read_ptr)

#define iSTB_Buffer_write_at(BUF) ((BUF)->buf + (BUF)->write_ptr)

#define iSTB_Buffer_commit_read(BUF, LEN) ((BUF)->read_ptr = ((BUF)->read_ptr + (LEN)) % (BUF)->buffersize)

#define iSTB_Buffer_commit_write(BUF, LEN) ((BUF)->write_ptr = ((BUF)->write_ptr + (LEN)) % (BUF)->buffersize)

#define iSTB_Buffer_available_data(BUF)   ((BUF->write_ptr < BUF->read_ptr)? (BUF->write_ptr+BUF->buffersize-BUF->read_ptr):(BUF->write_ptr-BUF->read_ptr))

#define iSTB_Buffer_available_space(BUF) ((BUF->write_ptr < BUF->read_ptr)? (BUF->read_ptr - BUF->write_ptr):(BUF->buffersize-BUF->write_ptr+BUF->read_ptr))


RingBuffer * iSTB_Buffer_create(int length)
{
    RingDBG("coming....\n");
    //188 is the size of one ts packet
    if (length%188)
    {
        RingDBG("the size is not fit:%d  \n",length);
        return 0;
    }

    RingBuffer *mybuffer = calloc(1, sizeof(RingBuffer)); 
    if (!mybuffer)
    {
         RingDBG("maybe no memory....\n");
         return -1;
    }
    pthread_mutex_init(&mybuffer->locker, NULL);
    mybuffer->buffersize  = length; 
    mybuffer->write_ptr=mybuffer->read_ptr= 0;
    mybuffer->buf  = calloc(mybuffer->buffersize, sizeof(char));
    if (!(mybuffer->buf))
    {
         RingDBG("maybe no memory....\n");
         free(mybuffer);
         return -1;
    }
    return mybuffer;
}

void iSTB_Buffer_destroy(RingBuffer *ringbuffer)
{
    RingDBG("coming..\n");
    if(ringbuffer) 
    {
        pthread_mutex_destroy(&ringbuffer->locker);
        free(ringbuffer->buf);
        free(ringbuffer);
        ringbuffer->buf=NULL;
        ringbuffer=NULL;
    }
}

int iSTB_Buffer_write(RingBuffer *ringbuffer, char *data, int length)
{

    void *result=NULL;
    int  lentotail =0;
    int  lentohead =0;
    int  validdatalen=0;
    int  validspacelen=0;
    Ring_NullPtrCheck(ringbuffer);
    Ring_NullPtrCheck(data);
    if (length % 188) 
    {
        RingDBG("lenght(%d) not fit \n",length);
        return -1;
    }
    pthread_mutex_lock(&ringbuffer->locker);
    //is some valid data in the buffer
    validdatalen=iSTB_Buffer_available_data(ringbuffer);
    if (validdatalen== 0) 
    {
        RingDBG(" no valid data in  buffer \n");
        iSTB_Buffer_empty(ringbuffer);
    }
    validspacelen=ringbuffer->buffersize-validdatalen;
    if (validspacelen < length)
    {
        RingDBG("Not enough space: %d need to write, %d available \n",
                length, iSTB_Buffer_available_space(ringbuffer));
        goto error;
    }
    lentotail        = ringbuffer->buffersize-ringbuffer->write_ptr;
    lentohead     = length-lentotail;
    if (lentotail >= length)//the len of data needing  write is no need to ring cycle
    {
         result= memcpy(iSTB_Buffer_write_at(ringbuffer), data, length); 
    }
    else
    {
         result = memcpy(iSTB_Buffer_write_at(ringbuffer), data, lentotail);
         if (!result)
         {
             RingDBG("now:Failed to write data into buffer \n");
             goto error;
         }
         result = memcpy(ringbuffer->buf, (data+lentotail), lentohead);
    }

    if (!result)
    {
        RingDBG("Failed to write data into buffer \n");
        goto error;
    }
    iSTB_Buffer_commit_write(ringbuffer, length);
    pthread_mutex_unlock(&ringbuffer->locker);
    return length;
error:
    pthread_mutex_unlock(&ringbuffer->locker);
    return -1;
}

int iSTB_Buffer_read(RingBuffer *buffer, char *target, int amount)
{
    void *result=NULL;
    int  lentotail =0;
    int  lentohead =0;
    Ring_NullPtrCheck(buffer);
    Ring_NullPtrCheck(target);
    pthread_mutex_lock(&buffer->locker);
    if (iSTB_Buffer_available_data(buffer) < amount)
    {
        #if 0
        RingDBG("Not enough in the buffer: has %d, needs %d \n",
                RingBuffer_available_data(buffer),amount);
       #endif
       pthread_mutex_unlock(&buffer->locker);
        return 0;
    }
    lentotail      = buffer->buffersize - buffer->read_ptr;
    lentohead   = amount-lentotail;

    if (lentotail >= amount)
    {
        result = memcpy(target, iSTB_Buffer_read_at(buffer), amount); 
    }
    else
    {
        result = memcpy(target, iSTB_Buffer_read_at(buffer),lentotail);
         if (!result)
         {
             RingDBG("now:Failed to write data into buffer \n");
             goto error;
         }
        result = memcpy(target, buffer->buf, lentohead);
    }

    if (!result)
    {
        RingDBG("Failed to write buffer into data \n");
        goto error;
    }
    iSTB_Buffer_commit_read(buffer, amount);
    //RingDBG("amount:%d \n",amount);
    pthread_mutex_unlock(&buffer->locker);
    return amount;
error:
    pthread_mutex_unlock(&buffer->locker);
    return -1;
}
#if 0
void * Ins_API_RingBuffer_GetHandle(void)
{
    return (void *)impl_RingBuffer;
}
int Ins_API_RingBuffer_create(int length)
{
    impl_RingBuffer=Ins_RingBuffer_create(length);
    if (g_Ringbuffer_Sem == NULL)
    {
        RingDBG("now create the sem!! \n");
        g_Ringbuffer_Sem = malloc(sizeof(sem_t));
        sem_init(g_Ringbuffer_Sem, 0 ,0);
        sem_post(g_Ringbuffer_Sem);
    }
    else
    {
         RingDBG("its already exsit!!(%d) \n",*g_Ringbuffer_Sem);
    }
    return 0;
}
void Ins_API_RingBuffer_destroy(void)
{
    sem_wait(g_Ringbuffer_Sem);
    //Ins_RingBuffer_destroy(impl_RingBuffer);
    RingDBG("coming..\n");
    if(impl_RingBuffer) 
    {
        free(impl_RingBuffer->buffer);
        impl_RingBuffer->buffer=NULL;
        free(impl_RingBuffer);       
        impl_RingBuffer=NULL;
        RingDBG("now destroy finish..\n");
    }
    sem_post(g_Ringbuffer_Sem);
    return;
}

int Ins_API_RingBuffer_write(char *data, int length)
{
    int ret=0;
    if (data ==NULL)
    {
        RingDBG("yx buffer write error...\n");
        return -1;
    }
    sem_wait(g_Ringbuffer_Sem); 

    //RingDBG("coming...(len=%d) \n",length);
    ret=Ins_RingBuffer_write(impl_RingBuffer,data,length);
    sem_post(g_Ringbuffer_Sem);
    return ret;
}

int Ins_API_RingBuffer_reset(void)
{
     sem_wait(g_Ringbuffer_Sem);
    if (impl_RingBuffer ==NULL)
    {
        RingDBG("handle error  \n");
        return -1;
    }
    impl_RingBuffer->start = impl_RingBuffer->end = 0;
    sem_post(g_Ringbuffer_Sem);
    return 0;
}



int Ins_API_RingBuffer_read( char *target, int amount)
{
    int ret=0;
    sem_wait(g_Ringbuffer_Sem);
    if (impl_RingBuffer ==NULL)
    {
        RingDBG("handle error  \n");
        return -1;
    }
    //188 is the size of one ts packet
    if (amount%188)
    {
       // RingDBG("the size is not fit:%d \n",amount);
        //return 0;
    }
    ret=Ins_RingBuffer_read(impl_RingBuffer,target,amount);
    sem_post(g_Ringbuffer_Sem);
    return ret;
}



#if 0

int Ins_API_RingBuffer_init(void)
{
}
int Ins_API_RingBuffer_create(int length)
{
    static int CreateID=0;
    RingBuffer *BufferHandle=NULL;
    BufferHandle=Ins_RingBuffer_create(length);
    if (!BufferHandle)
    {
         RingERR("create buffer failed \n");
         return -1;
    }
    BufferList_t *ImplBuflist=calloc(1,sizeof(BufferList));
    BufferList_t *Templist=&buflist_head; 
    if (!ImplBuflist)
    {
         RingERR("create list failed \n");
         return -1;
    }
    ImplBuflist->impl_buffer = BufferHandle;
    ImplBuflist->buffer_id     = ++CreateID; 
    ImplBuflist->next           =NULL;
    while (Templist->next)
    {
        Templist=Templist->next;
        Templist;
    }
}
#endif
#endif

#ifdef __cplusplus
}
#endif
