#ifndef ISTB_HTTPSERVER_H
#define ISTB_HTTPSERVER_H

#include "istb_mongoose.h"
#ifdef __cplusplus
extern "C" {
#endif

#define M_HTTP_DEBUG

#ifdef M_HTTP_DEBUG
#define mhttpDBG(format,args...) \
do{ \
printf("mhttpDBG[%s:%d]:"format"",__FUNCTION__,__LINE__,##args);\
}while(0)
#else  
#define mhttpDBG(format,args...)  do{}while(0)
#endif



pthread_t istb_httpserver_create(char * ip_port, char * doc_root,mg_io_fun * http_io_fun);
int  istb_httpserver_destroy(pthread_t tid);
#ifdef __cplusplus
}
#endif

#endif