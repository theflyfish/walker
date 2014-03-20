#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "istb_mongoose.h"
#include "istb_httpserver.h"
struct httptaskpara   
{  
    //int    port;
    char *ip_port;
    char *doc_root;   
    mg_io_fun *http_io_opreate_fun;
};  
int http_terminate=0;

// This function will be called by mongoose on every new request
static int user_defined_requst(struct mg_connection *conn) {
   char fifoname[128]={0};
   int  fifolen=0;
   mg_io_fun *http_io_opreate=NULL;
  mhttpDBG("Hello!request_method[%s], "
         "http_headers name[%s],value[%s] \n"
         "http_headers name[%s],value[%s] \n"
         "http_headers name[%s],value[%s] \n"
         "http_headers name[%s],value[%s] \n"
         "http_headers name[%s],value[%s] \n"
         "Requested URI is [%s], query string is [%s] \n",
                conn->request_method,
                conn->http_headers[0].name, conn->http_headers[0].value,
                conn->http_headers[1].name, conn->http_headers[1].value,
                conn->http_headers[2].name, conn->http_headers[2].value,
                conn->http_headers[3].name, conn->http_headers[3].value,
                conn->http_headers[4].name, conn->http_headers[4].value,
                conn->uri,
                 conn->query_string == NULL ? "(none)" : conn->query_string);
  strncpy(fifoname,(conn->uri)+1,strlen(conn->uri)-1);//kick the '/', as the uri is:  /tmp/istb-1.ts
  mhttpDBG("fifoname:%s \n",fifoname);
  http_io_opreate=(mg_io_fun *)conn->server_param;
  conn->userhandle=http_io_opreate->mg_io_open(fifoname,&fifolen);
  mhttpDBG("fifolen:%d \n",fifolen);
  mg_write_range(conn,fifoname,fifolen);

#if 0
  mg_send_header(conn, "Server", "Inspur Media Server");
  mg_send_header(conn, "Accept-Ranges", "bytes");
  mg_send_header(conn, "Connection", "close");
  mg_send_header(conn, "Content-Type", "video/mpeg");
  #endif
  return 0;
}



void * istb_httpserver_task(void * arg) {
  mhttpDBG("coming....\n");
  if (!arg) {
        mhttpDBG("param error ! \n");
        return 0;
  }
  struct mg_server *server;
  struct httptaskpara *para;
  char doc_rootstr[128]={0};
  char portstr[128]={0};
  para=(struct httptaskpara *)arg;
  mg_io_fun  * http_io_fun;
  if (!(para->doc_root)) {
      mhttpDBG("param error  \n");
      return  0;
  }
  strncpy(portstr,para->ip_port,sizeof(portstr));
  strncpy(doc_rootstr,para->doc_root,sizeof(doc_rootstr));
  http_io_fun=para->http_io_opreate_fun;
  free(para);
  para=NULL;
  mhttpDBG("thread  port=%s,root: %s \n",portstr,doc_rootstr);
  // Create and configure the server

  //http_io_fun.mg_io_close=
  server = mg_create_server((void *)http_io_fun);
  //server = mg_create_server(NULL);
  mg_set_option(server, "listening_port", portstr);
  mg_set_option(server, "document_root", doc_rootstr);
  mg_add_uri_handler(server, "/", user_defined_requst);

  // Serve request. Hit Ctrl-C to terminate the program
  mhttpDBG("Starting on port %s\n", mg_get_option(server, "listening_port"));
  while(!http_terminate) {
    mg_poll_server(server, 1000);
  }

  // Cleanup, and free server instance
  mg_destroy_server(&server);
  return 0 ;
}
pthread_t istb_httpserver_create(char * ip_port, char * doc_root,mg_io_fun * http_io_fun){
    int s=0;
    pthread_attr_t attr;
    pthread_t      ThreadId;
    //int ret =0;
    struct httptaskpara *para=NULL;
    if(!(para=(struct httptaskpara *)calloc(1,sizeof(struct httptaskpara)))){
        return 0;
    }
    para->ip_port=ip_port;
    para->doc_root=doc_root;
    para->http_io_opreate_fun=http_io_fun;
    s = pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    mhttpDBG("%s \n",para->doc_root);
    if(pthread_create (&ThreadId, &attr, (void *(*)(void *))istb_httpserver_task, (void *)para))
    {
        mhttpDBG("create http server thread error \n");
        pthread_attr_destroy(&attr);
        return 0;
    }
    mhttpDBG("ThreadId:%ld \n",ThreadId);
    s = pthread_attr_destroy(&attr);
    return ThreadId;
}


int istb_httpserver_destroy(pthread_t tid)
{
    int res;
    res = pthread_kill(tid, 0); //test is the pthread  alive
    mhttpDBG("thread  live status: %d \n",res);
    if (res ==3) {//ESRCH=3
        mhttpDBG("thread  has exsit!\n");
        return 0;
    }
    http_terminate = 1; 
    while (1) {
        res = pthread_kill(tid, 0); //test is the pthread  alive
        if (res ==3) {//ESRCH=3
            break;
        }
        usleep(1000*10);//10ms
    }
    mhttpDBG("thread  stop finish \n");
    http_terminate=0; //reset teminate flag
    return 0;
}

