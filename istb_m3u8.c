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
#include "istb_m3u8.h"


#define M3U8_SEGMENT_BUFFER_SIZE (1024*1024*2)//2m //(1024*1024*1024)

struct list_node { struct list_node *prev, *next; };
#define ISTB_LINKED_LIST_INIT(N)  ((N)->next = (N)->prev = (N))
#define ISTB_LINKED_LIST_DECLARE_AND_INIT(H)  struct ll H = { &H, &H }
#define ISTB_LINKED_LIST_ENTRY(P,T,N)  ((T *)((char *)(P) - offsetof(T, N)))
#define ISTB_LINKED_LIST_IS_EMPTY(N)  ((N)->next == (N))
#define ISTB_LINKED_LIST_FOREACH(H,N,T) \
  for (N = (H)->next, T = (N)->next; N != (H); N = (T), T = (N)->next)
#define ISTB_LINKED_LIST_ADD_TO_FRONT(H,N) do { ((H)->next)->prev = (N); \
  (N)->next = ((H)->next);  (N)->prev = (H); (H)->next = (N); } while (0)
#define ISTB_LINKED_LIST_ADD_TO_TAIL(H,N) do { ((H)->prev)->next = (N); \
  (N)->prev = ((H)->prev); (N)->next = (H); (H)->prev = (N); } while (0)
#define ISTB_LINKED_LIST_REMOVE(N) do { ((N)->next)->prev = ((N)->prev); \
  ((N)->prev)->next = ((N)->next); ISTB_LINKED_LIST_INIT(N); } while (0)


typedef struct {
    char   bufname[128];
    char * buf;
    int      bufsize;
    int      write_ptr;
    int      read_ptr;
}m3u8buf_t;

typedef struct{
    struct list_node link;
    m3u8buf_t *m3u8data;
    pthread_mutex_t locker;
}m3u8queue_t;

m3u8queue_t * queue_head=NULL;

struct m3u8para   
{   
    char * options;  
    READ_DATA_FUN read_data_fun;   
};  



struct options_t {
    char input_file[128];
    long segment_duration;
    char output_prefix[128];
    char m3u8_file[128];
    char * tmp_m3u8_file;
    char url_prefix[128];
    long num_segments;
};

static  M3U8_EVENT_CALLBACK event_callback_fun=NULL;

void handler(int signum);
static AVStream *add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream);
int write_index_file(const struct options_t, const unsigned int first_segment, const unsigned int last_segment, const int end);
void display_usage(void);
m3u8buf_t * istb_m3u8_buffer_create(char * name);
int istb_m3u8_buffer_write(m3u8buf_t * m3u8buffer,char *writebuf,int len);
int istb_m3u8_buffer_read(m3u8buf_t * m3u8buffer,char *readbuf,int len);
int istb_m3u8_buffer_destroy(m3u8buf_t * m3u8buffer);
int istb_m3u8_queue_head_update(m3u8buf_t * m3u8buf);
void istb_m3u8_queue_init(void);
int istb_m3u8_queue_add(m3u8buf_t * m3u8buffer);
int istb_m3u8_queue_remove(void);
int istb_m3u8_queue_destroy(m3u8queue_t * qhead);


int terminate = 0;


void handler(int signum) {
    (void)signum;
    terminate = 1;
}

static AVStream *add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream) {
    AVCodecContext *input_codec_context;
    AVCodecContext *output_codec_context;
    AVStream *output_stream;

    output_stream = avformat_new_stream(output_format_context, 0);
    if (!output_stream) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }

    input_codec_context = input_stream->codec;
    output_codec_context = output_stream->codec;

    output_codec_context->codec_id = input_codec_context->codec_id;
    output_codec_context->codec_type = input_codec_context->codec_type;
    output_codec_context->codec_tag = input_codec_context->codec_tag;
    output_codec_context->bit_rate = input_codec_context->bit_rate;
    output_codec_context->extradata = input_codec_context->extradata;
    output_codec_context->extradata_size = input_codec_context->extradata_size;

    if(av_q2d(input_codec_context->time_base) * input_codec_context->ticks_per_frame > av_q2d(input_stream->time_base) && av_q2d(input_stream->time_base) < 1.0/1000) {
        output_codec_context->time_base = input_codec_context->time_base;
        output_codec_context->time_base.num *= input_codec_context->ticks_per_frame;
    }
    else {
        output_codec_context->time_base = input_stream->time_base;
    }

    switch (input_codec_context->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            output_codec_context->channel_layout = input_codec_context->channel_layout;
            output_codec_context->sample_rate = input_codec_context->sample_rate;
            output_codec_context->channels = input_codec_context->channels;
            output_codec_context->frame_size = input_codec_context->frame_size;
            if ((input_codec_context->block_align == 1 && input_codec_context->codec_id == CODEC_ID_MP3) || input_codec_context->codec_id == CODEC_ID_AC3) {
                output_codec_context->block_align = 0;
            }
            else {
                output_codec_context->block_align = input_codec_context->block_align;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            output_codec_context->pix_fmt = input_codec_context->pix_fmt;
            output_codec_context->width = input_codec_context->width;
            output_codec_context->height = input_codec_context->height;
            output_codec_context->has_b_frames = input_codec_context->has_b_frames;

            if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
                output_codec_context->flags |= CODEC_FLAG_GLOBAL_HEADER;
            }
            break;
    default:
        break;
    }

    return output_stream;
}

int write_index_file(const struct options_t options, const unsigned int first_segment, const unsigned int last_segment, const int end) {
    char *write_buf;
    unsigned int i;
    m3u8buf_t * m3u8_tmp=istb_m3u8_buffer_create(options.tmp_m3u8_file);
    write_buf = malloc(sizeof(char) * 1024);
    if (!write_buf) {
        fprintf(stderr, "Could not allocate write buffer for index file, index file will be invalid\n");
        istb_m3u8_buffer_destroy(m3u8_tmp);
        return -1;
    }
    if (options.num_segments) {
        snprintf(write_buf, 1024, "#EXTM3U\n#EXT-X-TARGETDURATION:%lu\n#EXT-X-MEDIA-SEQUENCE:%u\n", options.segment_duration, first_segment);
    }
    else {
        snprintf(write_buf, 1024, "#EXTM3U\n#EXT-X-TARGETDURATION:%lu\n", options.segment_duration);
    }
    if ((istb_m3u8_buffer_write(m3u8_tmp,write_buf, strlen(write_buf)))<0) {
        m3u8DBG("could not write to m3u8 index file, will not continue writing to index file \n ");
        istb_m3u8_buffer_destroy(m3u8_tmp);
        free(write_buf);
        write_buf=NULL;
        return -1;
    }

    for (i = first_segment; i <= last_segment; i++) {
        snprintf(write_buf, 1024, "#EXTINF:%lu,\n%s%s-%u.ts\n", options.segment_duration, options.url_prefix, options.output_prefix, i);
        if ((istb_m3u8_buffer_write(m3u8_tmp,write_buf, strlen(write_buf)))<0) {
            m3u8DBG("could not write to m3u8 index file, will not continue writing to index file \n ");
            istb_m3u8_buffer_destroy(m3u8_tmp);
            free(write_buf);
            write_buf=NULL;
            return -1;
        }
    }

    if (end) {
        snprintf(write_buf, 1024, "#EXT-X-ENDLIST\n");
        if ((istb_m3u8_buffer_write(m3u8_tmp,write_buf, strlen(write_buf)))<0) {
            m3u8DBG("Could not write last file and endlist tag to m3u8 index file \n ");
            istb_m3u8_buffer_destroy(m3u8_tmp);
            free(write_buf);
            write_buf=NULL;
            return -1;
        }
    }
    free(write_buf);
    istb_m3u8_queue_head_update(m3u8_tmp);
    return 0;
}

void display_usage(void)
{
    printf("Usage: m3u8-sementer [OPTION]...\n");
    printf("\n");
    printf("HTTP Live Streaming - Segments TS file and creates M3U8 index.");
    printf("\n");
    printf("\t-i, --input FILE             TS file to segment (Use - for stdin)\n");
    printf("\t-d, --duration SECONDS       Duration of each segment (default: 10 seconds)\n");
    printf("\t-p, --output-prefix PREFIX   Prefix for the TS segments, will be appended\n");
    printf("\t                             with -1.ts, -2.ts etc\n");
    printf("\t-m, --m3u8-file FILE         M3U8 output filename\n");
    printf("\t-u, --url-prefix PREFIX      Prefix for web address of segments, e.g. http://example.org/video/\n");
    printf("\t-n, --num-segment NUMBER     Number of segments to keep on disk\n");
    printf("\t-h, --help                   This help\n");
    printf("\n");
    printf("\n");

    exit(0);
}





#if 0
//#define FILE_SERVER_BUFF_SIZE (188*5)
#define FILE_SERVER_BUFF_SIZE (188*10)
#define VLC_Test_File_Path  "/home/yuxiang/workspace/m3u8-test/test.ts"
static int isNeedShutDown=0;
void Buffer_FileServer_Task(void)
{
    char ReadBuf_file[FILE_SERVER_BUFF_SIZE]={0};
    FILE * fp_VlCTest=NULL;
    int have_read=0;
    long int file_size=0;
    long int file_pos=0;
    int have_send=0;
    m3u8DBG("coming..\n");
    fp_VlCTest = fopen(VLC_Test_File_Path, "rb+" );
    if(fp_VlCTest == NULL)
    {
        m3u8DBG("can not open file!\n");
        return ;
    }
    fseek(fp_VlCTest,0,SEEK_END); 
    file_size = ftell(fp_VlCTest);
    //fseek(fp_VlCTest,0,SEEK_SET); 
    rewind(fp_VlCTest);
    m3u8DBG(":xxxfile_size:%ld,fp:%p \n",file_size,fp_VlCTest);
    do{ 
        if (isNeedShutDown)
        {
            m3u8DBG("stop file server \n");
            break;
        }
        have_read = fread(ReadBuf_file, sizeof(char), FILE_SERVER_BUFF_SIZE, fp_VlCTest); 
        if (have_read!=FILE_SERVER_BUFF_SIZE)
        {
            if ((file_pos+have_read)==file_size)
            {
                m3u8DBG("read file get end!\n"); 
            }
            else
            {
                m3u8DBG("read file error!\n"); 
                break;
            }
        }
        file_pos += have_read;
        //m3u8DBG(" send live streamer \n");
        Ins_API_RingBuffer_write(ReadBuf_file,FILE_SERVER_BUFF_SIZE);
        //m3u8DBG("%s:have send=%d \n",__FUNCTION__,have_send);
        //usleep(100);//0.1ms
        usleep(1000*10);//10ms

    }while(file_pos < file_size);
    fclose(fp_VlCTest);
    m3u8DBG("file streamer push finished !\n");
    usleep(1000*1000);//1s
    terminate=1;
    return; 
}

#define BF_RINGBUF_SIZE (188*1024*10*10*5)
int Buffer_FileServer_Start(void)
{
    int s=0;
    pthread_attr_t attr;
    pthread_t      ThreadId;
    int ret =0;

    Ins_API_RingBuffer_create(BF_RINGBUF_SIZE); 

    s = pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    
    if(pthread_create (&ThreadId, &attr, (void *(*)(void *))Buffer_FileServer_Task, NULL))
    {
        m3u8DBG("create http server thread error \n");
        pthread_attr_destroy(&attr);
        return -1;
    }
    s = pthread_attr_destroy(&attr);
    return 0;
}


int read_data(void *opaque, uint8_t *buf, int buf_size) {
//	UdpParam udphead;
	int size = buf_size;
	int ret;
    int filled=0;
	//printf("read data %d\n", buf_size);
	do {
        if (terminate){
            break;
        }
        ret = Ins_API_RingBuffer_read(buf + filled, size); 
        filled+=ret;
        size-=ret;
	} while (size);

	//printf("read data Ok %d\n", filled);
	return filled;
}
#endif
#define READ_BUF_SIZE	4096*1024  //4096*500  4m
#define WRITE_BUF_SIZE	4096*1024*2 //4096*500


int istb_m3u8_write_data(void *opaque, uint8_t *buf, int buf_size) {
	int size = buf_size;
	int ret=0;
    m3u8buf_t *m3u8buffer=(m3u8buf_t *)opaque;
    if (m3u8buffer !=NULL) {
        ret=istb_m3u8_buffer_write(m3u8buffer,(char *)buf,size);
    }
	//printf("read data Ok %d\n", filled);
	return ret;
}



int istb_m3u8_event_report(int eventtype)
{
    if (event_callback_fun) {
        m3u8DBG("coming....\n");
        event_callback_fun(eventtype);
    }
    return 0;
}
int istb_m3u8_event_subscribe(M3U8_EVENT_CALLBACK Callback)
{
    if (Callback) {
        m3u8DBG("coming....\n");
        event_callback_fun = Callback; 
    }
    return 0;
}


//m3u8-segmenter -d 10 -p tmp/big_buck_bunny -m tmp/big_buck.m3u8 -u http://inodes.org/bigbuck/

int istb_m3u8_parse_options(struct options_t * optionsp,  char * strings){
    char *tmp=NULL;
    char *next=NULL;
    char *local=strings;
    char ch;
    char test;
    if (optionsp==NULL || strings ==NULL) {
        return -1;
    }
    m3u8DBG("coming... \n");
    tmp=local;
    while (1) {
        tmp = strstr(tmp,"-");
        if (!tmp) {
            break;
        }
        tmp+=1;
        test=*tmp;
        switch (ch=*(tmp++)) {
        case 'd':
           optionsp->segment_duration=strtol(tmp,&next,10);
           m3u8DBG("segment_duration:%ld \n",optionsp->segment_duration);
           tmp=next;
            break;
        case 'p':
            while (*tmp==' ') {
                tmp++;
            }
            if (!(next = strstr(tmp, " -"))) {
                strncpy(optionsp->output_prefix,tmp,strlen(tmp));
                tmp+=strlen(tmp);
            }
            else{
                strncpy(optionsp->output_prefix,tmp,next-tmp);
                tmp=next;
            }
            m3u8DBG("output_prefix:%s\n",optionsp->output_prefix);

            break;
        case 'm':
            while (*tmp==' ') {
                tmp++;
            }
            if(!(next=strstr(tmp," -"))){
                strncpy(optionsp->m3u8_file,tmp,strlen(tmp));
                tmp+=strlen(tmp);
            }
            else{
                strncpy(optionsp->m3u8_file,tmp,next-tmp);
                tmp=next;
            }
            m3u8DBG("m3u8_file:%s\n",optionsp->m3u8_file);
            break;
        case 'u':
            while (*tmp==' ') {
                tmp++;
            }
            if(!(next=strstr(tmp," -"))){
                strncpy(optionsp->url_prefix,tmp,strlen(tmp));
                tmp+=strlen(tmp);
            }
            else{
                strncpy(optionsp->url_prefix,tmp,next-tmp);
                tmp=next;
            }
            m3u8DBG("url_prefix:%s\n",optionsp->url_prefix);
            break;
        case 'n':
            optionsp->num_segments=strtol(tmp,&next,10);
            m3u8DBG("num_segments:%ld\n",optionsp->num_segments);
            tmp=next;
            break;
        }
    }
    return 0;
}

#if 1
//void main(void)
int istb_m3u8_task(char * optionstrings, READ_DATA_FUN read_data_fun)
{
    double prev_segment_time = 0;
    unsigned int output_index = 1;
    AVIOContext * pb = NULL;
    AVInputFormat *piFmt = NULL;
    AVOutputFormat *ofmt=NULL;
    AVFormatContext *ic = NULL;
    AVFormatContext *oc= NULL;
    AVStream *video_st = NULL;
    AVStream *audio_st = NULL;
    AVCodec *codec=NULL;
    char *output_filename=NULL;
    char *remove_filename=NULL;
    int video_index = -1;
    int audio_index = -1;
    unsigned int first_segment = 1;
    unsigned int last_segment = 0;
    int write_index = 1;
    int decode_done;
    char *dot;
    int ret;
    unsigned int i;
    int remove_file;
    struct sigaction act;

    //int opt;
    //int longindex;
    //char *endptr;
    struct options_t options;

     m3u8buf_t *m3u8_segment_buf=NULL;
     uint8_t      *writebuf=NULL;
     uint8_t      *readbuf=NULL;


    memset(&options, 0 ,sizeof(options));
    istb_m3u8_parse_options(&options,optionstrings);
    istb_m3u8_queue_init();
    /* Check required args where set*/
    if (options.input_file == NULL ) {
        fprintf(stderr, "Please specify an input file.\n");
        goto fail;
    }
    if (options.output_prefix == NULL) {
        fprintf(stderr, "Please specify an output prefix.\n");
        goto fail;
    }
    if (options.m3u8_file == NULL) {
        fprintf(stderr, "Please specify an m3u8 output file.\n");
        goto fail;
    }
    if (options.url_prefix == NULL) {
        fprintf(stderr, "Please specify a url prefix.\n");
        goto fail;;
    }
    av_register_all();
    remove_filename = malloc(sizeof(char) * (strlen(options.output_prefix) + 15));
    if (!remove_filename) {
        fprintf(stderr, "Could not allocate space for remove filenames\n");
        goto fail;
    }

    output_filename = malloc(sizeof(char) * (strlen(options.output_prefix) + 15));
    if (!output_filename) {
        fprintf(stderr, "Could not allocate space for output filenames\n");
        goto fail;
    }
    options.tmp_m3u8_file = malloc(strlen(options.m3u8_file) + 2);
    if (!options.tmp_m3u8_file) {
        fprintf(stderr, "Could not allocate space for temporary index filename\n");
        goto fail;
    }
    // Use a dotfile as a temporary file
    strncpy(options.tmp_m3u8_file, options.m3u8_file, strlen(options.m3u8_file) + 2);
    dot = strrchr(options.tmp_m3u8_file, '/');
    dot = dot ? dot + 1 : options.tmp_m3u8_file;
    memmove(dot + 1, dot, strlen(dot));
    *dot = '.';
    m3u8DBG("options.tmp_m3u8_file:%s \n",options.tmp_m3u8_file);

    readbuf = av_mallocz(sizeof(uint8_t)*READ_BUF_SIZE);
    pb = avio_alloc_context(readbuf, READ_BUF_SIZE, 0, NULL, read_data_fun, NULL, NULL);
    if (!pb) {
        fprintf(stderr, "avio alloc failed!\n");
        goto fail;
    }
    piFmt = av_find_input_format("mpegts");//mpegts
    if (!piFmt) {
        fprintf(stderr, "Could not find MPEG-TS demuxer\n");
        goto fail;
    }else {
        fprintf(stdout, "yuxiang find success!\n");
        fprintf(stdout, "yuxiang format: %s[%s]\n", piFmt->name, piFmt->long_name);
    }

    ic = avformat_alloc_context();
    ic->pb = pb;
    if (avformat_open_input(&ic, "", piFmt, NULL) < 0) {
        fprintf(stderr, "avformat open failed.\n");
        goto fail;
    } else {
        fprintf(stdout, "open stream success!\n");
    }

    if (avformat_find_stream_info(ic,NULL) < 0) {
        fprintf(stderr, "could not fine stream.\n");
        goto fail;
    }
    av_dump_format(ic, 0, "", 0);


    ofmt = av_guess_format("mpegts", NULL, NULL);
    if (!ofmt) {
        fprintf(stderr, "Could not find MPEG-TS muxer\n");
        goto fail;
    }
    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Could not allocated output context");
        goto fail;
    }
    oc->oformat = ofmt;

    for (i = 0; i < ic->nb_streams && (video_index < 0 || audio_index < 0); i++) {
        switch (ic->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                video_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                video_st = add_output_stream(oc, ic->streams[i]);
                break;
            case AVMEDIA_TYPE_AUDIO:
                audio_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                audio_st = add_output_stream(oc, ic->streams[i]);
                break;
            default:
                ic->streams[i]->discard = AVDISCARD_ALL;
                break;
        }
    }

    // Don't print warnings when PTS and DTS are identical.
    ic->flags |= AVFMT_FLAG_IGNDTS;

    av_dump_format(oc, 0, options.output_prefix, 1);

    if (video_st) {
      codec = avcodec_find_decoder(video_st->codec->codec_id);
      if (!codec) {
          fprintf(stderr, "Could not find video decoder %x, key frames will not be honored\n", video_st->codec->codec_id);
      }
      if (avcodec_open2(video_st->codec, codec, NULL) < 0) {
          fprintf(stderr, "Could not open video decoder, key frames will not be honored\n");
      }

    }

    snprintf(output_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, output_index++);
#if 1

    m3u8_segment_buf=istb_m3u8_buffer_create(output_filename);
    istb_m3u8_queue_add(m3u8_segment_buf);//add the new segment to list queue
    writebuf = av_mallocz(sizeof(uint8_t)*WRITE_BUF_SIZE);
    oc->pb = avio_alloc_context(writebuf, WRITE_BUF_SIZE, AVIO_FLAG_WRITE, (void *)m3u8_segment_buf, NULL, istb_m3u8_write_data, NULL);
    if (!oc->pb) {
        fprintf(stderr, "Could not open '%s'\n", output_filename);
        ret = AVERROR(ENOMEM);
        goto fail;
    }
#else
    if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Could not open '%s'\n", output_filename);
        exit(1);
    }
#endif
    if (avformat_write_header(oc, NULL)) {
        fprintf(stderr, "Could not write mpegts header to first output file\n");
        goto fail;
    }

    write_index = !write_index_file(options, first_segment, last_segment, 0);
    /* Setup signals */
    memset(&act, 0, sizeof(act));
    act.sa_handler = &handler;

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    do {
        double segment_time = prev_segment_time;
        AVPacket packet;

        if (terminate) {
          m3u8DBG("now need terminal stop!!!\n");
          break;
        }

        decode_done = av_read_frame(ic, &packet);
        if (decode_done < 0) {
            break;
        }

        if (av_dup_packet(&packet) < 0) {
            fprintf(stderr, "Could not duplicate packet");
            av_free_packet(&packet);
            break;
        }

        // Use video stream as time base and split at keyframes. Otherwise use audio stream
        if (packet.stream_index == video_index && (packet.flags & AV_PKT_FLAG_KEY)) {
            segment_time = packet.pts * av_q2d(video_st->time_base);
        }
        else if (video_index < 0) {
            segment_time = packet.pts * av_q2d(audio_st->time_base);
        }
        else {
          segment_time = prev_segment_time;
        }


        if (segment_time - prev_segment_time >= options.segment_duration) {
            av_write_trailer(oc);   // close ts file and free memory
            avio_flush(oc->pb);
            //avio_close(oc->pb);
            av_freep(oc->pb);//yx add it
            free(writebuf);//yx add it
            writebuf=NULL;

            if (options.num_segments && (int)(last_segment - first_segment) >= options.num_segments - 1) {
                remove_file = 1;
                first_segment++;
            }
            else {
                remove_file = 0;
            }
            m3u8DBG("write_index=%d,remove_file=%d  \n",write_index,remove_file);
            if (write_index) {
                write_index = !write_index_file(options, first_segment, ++last_segment, 0);
            }

            if (remove_file) {
                snprintf(remove_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, first_segment - 1);
                m3u8DBG("remove_filename:%s \n",remove_filename);
                istb_m3u8_queue_remove();
                //remove(remove_filename);
            }

            snprintf(output_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, output_index++);
#if 1
            m3u8_segment_buf=istb_m3u8_buffer_create(output_filename);
            istb_m3u8_queue_add(m3u8_segment_buf);//add the new segment to list queue
            writebuf = av_mallocz(sizeof(uint8_t)*WRITE_BUF_SIZE);
            oc->pb = avio_alloc_context(writebuf, WRITE_BUF_SIZE, AVIO_FLAG_WRITE,(void *)m3u8_segment_buf, NULL, istb_m3u8_write_data, NULL);
            if (!oc->pb) {
                fprintf(stderr, "alloc context output failed \n");
                ret = AVERROR(ENOMEM);
                goto fail;
            }
#else
            if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
                fprintf(stderr, "Could not open '%s'\n", output_filename);
                break;
            }
#endif
            // Write a new header at the start of each file
            if (avformat_write_header(oc, NULL)) {
              fprintf(stderr, "Could not write mpegts header to first output file\n");
              goto fail;
            }
            prev_segment_time = segment_time;
            m3u8DBG("last_segment=%d \n",last_segment);
           if (last_segment==3) {
                   istb_m3u8_event_report(M3U8_SEGMENT_READY); 
           }
        }

        ret = av_interleaved_write_frame(oc, &packet);
        if (ret < 0) {
            //fprintf(stderr, "Warning: Could not write frame of stream\n");
        }
        else if (ret > 0) {
            fprintf(stderr, "End of stream requested\n");
            av_free_packet(&packet);
            break;
        }
        av_free_packet(&packet);


    } while (!decode_done);

    av_write_trailer(oc);
    m3u8DBG("hahaha\n");
    if (options.num_segments && (int)(last_segment - first_segment) >= options.num_segments - 1) {
        remove_file = 1;
        first_segment++;
    }
    else {
        remove_file = 0;
    }
    if (write_index) {
        write_index_file(options, first_segment, ++last_segment, 1);
    }
    m3u8DBG("hahahahbbbbb \n");
    if (remove_file) {
        snprintf(remove_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, first_segment - 1);
        //remove(remove_filename);
        m3u8DBG("remove_filename:%s \n",remove_filename);
        istb_m3u8_queue_remove();
    }


fail:
    m3u8DBG("hahahcccc \n");
    if (video_st) {
      avcodec_close(video_st->codec);
    }
    if (oc){
        for (i = 0; i < oc->nb_streams; i++) {
            av_freep(&oc->streams[i]->codec);
            av_freep(&oc->streams[i]);
        }
        //avio_close(oc->pb);    //yx do it   
        if (oc->pb) {
            av_freep(oc->pb); //yx add it
        }
        av_free(oc);
    }
    //if the READ_BUF_SIZE <32 k,should call this free ,but,
    //if the READ_BUF_SIZE >32k,the readbuff point maybe realloc by the ffmpeg modules ,
    //so never do this because it  cause crash!!!!
    #if 0
    if (readbuf!=NULL) {
        free(readbuf);
        readbuf=NULL;
    }
   #endif
    if (ic){
        if (ic->pb) {
            av_freep(ic->pb); //yx add it
        }
        av_free(ic);
    }
    if (writebuf) {
        free(writebuf);
        writebuf=NULL;
    }

    if (remove_filename) {
        free(remove_filename);
        remove_filename=NULL;
    }
    if (output_filename) {
        free(output_filename);
        output_filename=NULL;
    }
    if (options.tmp_m3u8_file) {
        free(options.tmp_m3u8_file);
        options.tmp_m3u8_file=NULL;
    }
    istb_m3u8_queue_destroy(queue_head);
    istb_m3u8_event_report(M3U8_SEGMENT_FINISH);
    return 0;
}
#else
//void main(void)
int istb_m3u8_task(char * optionstrings, READ_DATA_FUN read_data_fun)
{
    double prev_segment_time = 0;
    unsigned int output_index = 1;
    AVInputFormat *ifmt;
    AVOutputFormat *ofmt;
    AVFormatContext *ic = NULL;
    AVFormatContext *oc;
    AVStream *video_st = NULL;
    AVStream *audio_st = NULL;
    AVCodec *codec;
    char *output_filename;
    char *remove_filename;
    int video_index = -1;
    int audio_index = -1;
    unsigned int first_segment = 1;
    unsigned int last_segment = 0;
    int write_index = 1;
    int decode_done;
    char *dot;
    int ret;
    unsigned int i;
    int remove_file;
    struct sigaction act;

    int opt;
    int longindex;
    char *endptr;
    struct options_t options;
    memset(&options, 0 ,sizeof(options));
    istb_m3u8_parse_options(&options,optionstrings);
    #if 0
    /* Set some defaults */
    options.segment_duration = 10;
    options.num_segments = 0;
    options.input_file = "";
    options.output_prefix = "tmp/yuxiang-test";
    options.m3u8_file = "yuxiang.m3u8";
    //options.url_prefix = "http://192.168.1.111:8080/";
    options.url_prefix = "";
    //options.num_segments = 3;//for living test
     fprintf(stderr, "Please specify an input file.\n");
     #endif
    /* Check required args where set*/
    if (options.input_file == NULL) {
        fprintf(stderr, "Please specify an input file.\n");
        exit(1);
    }

    if (options.output_prefix == NULL) {
        fprintf(stderr, "Please specify an output prefix.\n");
        exit(1);
    }

    if (options.m3u8_file == NULL) {
        fprintf(stderr, "Please specify an m3u8 output file.\n");
        exit(1);
    }

    if (options.url_prefix == NULL) {
        fprintf(stderr, "Please specify a url prefix.\n");
        exit(1);
    }
    av_register_all();
    remove_filename = malloc(sizeof(char) * (strlen(options.output_prefix) + 15));
    if (!remove_filename) {
        fprintf(stderr, "Could not allocate space for remove filenames\n");
        exit(1);
    }

    output_filename = malloc(sizeof(char) * (strlen(options.output_prefix) + 15));
    if (!output_filename) {
        fprintf(stderr, "Could not allocate space for output filenames\n");
        exit(1);
    }

    options.tmp_m3u8_file = malloc(strlen(options.m3u8_file) + 2);
    if (!options.tmp_m3u8_file) {
        fprintf(stderr, "Could not allocate space for temporary index filename\n");
        exit(1);
    }

    // Use a dotfile as a temporary file
    strncpy(options.tmp_m3u8_file, options.m3u8_file, strlen(options.m3u8_file) + 2);
    dot = strrchr(options.tmp_m3u8_file, '/');
    dot = dot ? dot + 1 : options.tmp_m3u8_file;
    memmove(dot + 1, dot, strlen(dot));
    *dot = '.';


#if 0
    double prev_segment_time = 0;
    unsigned int output_index = 1;
    AVInputFormat *ifmt;
    AVOutputFormat *ofmt;
    AVFormatContext *ic = NULL;
    AVFormatContext *oc;
    AVStream *video_st = NULL;
    AVStream *audio_st = NULL;
    AVCodec *codec;
    char *output_filename;
    char *remove_filename;
    ret = avformat_open_input(&ic, options.input_file, ifmt, NULL);
    if (ret != 0) {
        fprintf(stderr, "Could not open input file, make sure it is an mpegts file: %d\n", ret);
        exit(1);
    }

    if (avformat_find_stream_info(ic, NULL) < 0) {
        fprintf(stderr, "Could not read stream information\n");
        exit(1);
    }
#else

    AVIOContext * pb = NULL;
    AVInputFormat *piFmt = NULL;
   // AVFormatContext *pFmt = NULL;
   uint8_t *buf = av_mallocz(sizeof(uint8_t)*READ_BUF_SIZE);

   // pb = avio_alloc_context(buf, READ_BUF_SIZE, 0, NULL, read_data, NULL, NULL);
    pb = avio_alloc_context(buf, READ_BUF_SIZE, 0, NULL, read_data_fun, NULL, NULL);
    if (!pb) {
        fprintf(stderr, "avio alloc failed!\n");
        return -1;
    }
    #if 0
    if (av_probe_input_buffer(pb, &piFmt, "", NULL, 0, 0) < 0) {
        fprintf(stderr, "probe failed!\n");
//			return -1;
    } else {
        fprintf(stdout, "probe success!\n");
        fprintf(stdout, "format: %s[%s]\n", piFmt->name, piFmt->long_name);
    }
    #else
    piFmt = av_find_input_format("mpegts");//mpegts
    if (!piFmt) {
        fprintf(stderr, "Could not find MPEG-TS demuxer\n");
        exit(1);
    }else {
        fprintf(stdout, "yuxiang find success!\n");
        fprintf(stdout, "yuxiang format: %s[%s]\n", piFmt->name, piFmt->long_name);
    }
    #endif
    ic = avformat_alloc_context();
    ic->pb = pb;
    if (avformat_open_input(&ic, "", piFmt, NULL) < 0) {
        fprintf(stderr, "avformat open failed.\n");
        return -1;
    } else {
        fprintf(stdout, "open stream success!\n");
    }

    if (avformat_find_stream_info(ic,NULL) < 0) {
        fprintf(stderr, "could not fine stream.\n");
        return -1;
    }
    av_dump_format(ic, 0, "", 0);
#endif

    ofmt = av_guess_format("mpegts", NULL, NULL);
    if (!ofmt) {
        fprintf(stderr, "Could not find MPEG-TS muxer\n");
        exit(1);
    }
    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Could not allocated output context");
        exit(1);
    }
    oc->oformat = ofmt;

    for (i = 0; i < ic->nb_streams && (video_index < 0 || audio_index < 0); i++) {
        switch (ic->streams[i]->codec->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                video_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                video_st = add_output_stream(oc, ic->streams[i]);
                break;
            case AVMEDIA_TYPE_AUDIO:
                audio_index = i;
                ic->streams[i]->discard = AVDISCARD_NONE;
                audio_st = add_output_stream(oc, ic->streams[i]);
                break;
            default:
                ic->streams[i]->discard = AVDISCARD_ALL;
                break;
        }
    }

    // Don't print warnings when PTS and DTS are identical.
    ic->flags |= AVFMT_FLAG_IGNDTS;

    av_dump_format(oc, 0, options.output_prefix, 1);

    if (video_st) {
      codec = avcodec_find_decoder(video_st->codec->codec_id);
      if (!codec) {
          fprintf(stderr, "Could not find video decoder %x, key frames will not be honored\n", video_st->codec->codec_id);
      }

      if (avcodec_open2(video_st->codec, codec, NULL) < 0) {
          fprintf(stderr, "Could not open video decoder, key frames will not be honored\n");
      }
    }


    snprintf(output_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, output_index++);
    if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
        fprintf(stderr, "Could not open '%s'\n", output_filename);
        exit(1);
    }

    if (avformat_write_header(oc, NULL)) {
        fprintf(stderr, "Could not write mpegts header to first output file\n");
        exit(1);
    }

    write_index = !write_index_file(options, first_segment, last_segment, 0);

    /* Setup signals */
    memset(&act, 0, sizeof(act));
    act.sa_handler = &handler;

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    do {
        double segment_time = prev_segment_time;
        AVPacket packet;

        if (terminate) {
          m3u8DBG("now need terminal stop!!!\n");
          break;
        }

        decode_done = av_read_frame(ic, &packet);
        if (decode_done < 0) {
            break;
        }

        if (av_dup_packet(&packet) < 0) {
            fprintf(stderr, "Could not duplicate packet");
            av_free_packet(&packet);
            break;
        }

        // Use video stream as time base and split at keyframes. Otherwise use audio stream
        if (packet.stream_index == video_index && (packet.flags & AV_PKT_FLAG_KEY)) {
            segment_time = packet.pts * av_q2d(video_st->time_base);
        }
        else if (video_index < 0) {
            segment_time = packet.pts * av_q2d(audio_st->time_base);
        }
        else {
          segment_time = prev_segment_time;
        }


        if (segment_time - prev_segment_time >= options.segment_duration) {
            av_write_trailer(oc);   // close ts file and free memory
            avio_flush(oc->pb);
            avio_close(oc->pb);

            if (options.num_segments && (int)(last_segment - first_segment) >= options.num_segments - 1) {
                remove_file = 1;
                first_segment++;
            }
            else {
                remove_file = 0;
            }

            if (write_index) {
                write_index = !write_index_file(options, first_segment, ++last_segment, 0);
            }

            if (remove_file) {
                snprintf(remove_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, first_segment - 1);
                remove(remove_filename);
            }

            snprintf(output_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, output_index++);
            if (avio_open(&oc->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
                fprintf(stderr, "Could not open '%s'\n", output_filename);
                break;
            }

            // Write a new header at the start of each file
            if (avformat_write_header(oc, NULL)) {
              fprintf(stderr, "Could not write mpegts header to first output file\n");
              exit(1);
            }
            prev_segment_time = segment_time;
            m3u8DBG("last_segment=%d \n",last_segment);
           if (last_segment==3) {
                   istb_m3u8_event_report(M3U8_SEGMENT_READY); 
           }
        }

        ret = av_interleaved_write_frame(oc, &packet);
        if (ret < 0) {
            fprintf(stderr, "Warning: Could not write frame of stream\n");
        }
        else if (ret > 0) {
            fprintf(stderr, "End of stream requested\n");
            av_free_packet(&packet);
            break;
        }
        av_free_packet(&packet);


    } while (!decode_done);

    av_write_trailer(oc);

    if (video_st) {
      avcodec_close(video_st->codec);
    }

    for(i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    avio_close(oc->pb);
    av_free(oc);

    if (options.num_segments && (int)(last_segment - first_segment) >= options.num_segments - 1) {
        remove_file = 1;
        first_segment++;
    }
    else {
        remove_file = 0;
    }

    if (write_index) {
        write_index_file(options, first_segment, ++last_segment, 1);
    }

    if (remove_file) {
        snprintf(remove_filename, strlen(options.output_prefix) + 15, "%s-%u.ts", options.output_prefix, first_segment - 1);
        remove(remove_filename);
    }
    istb_m3u8_event_report(M3U8_SEGMENT_FINISH);
    return 0;
}
#endif
void * istb_m3u8_thread(void * arg)
{
    if (!arg) {
        m3u8DBG("param error ! \n");
        return 0;
    }
    struct m3u8para *para; 
    para=(struct m3u8para *)arg;
    m3u8DBG("%s \n",para->options);
    istb_m3u8_task(para->options,para->read_data_fun);
    free(para);
    para=NULL;
    return 0;
}

pthread_t  istb_m3u8_start(char * optionstrings, READ_DATA_FUN read_data_fun){
    int s=0;
    pthread_attr_t attr;
    pthread_t      ThreadId;
    //int ret =0;
    struct m3u8para *para=NULL; 
    if (!read_data_fun || !optionstrings) {
        m3u8DBG("param error ! \n");
        return 0;
    }
    if(!(para=(struct m3u8para *)calloc(1,sizeof(struct m3u8para)))){
        return 0;
    }
    para->options           = optionstrings; 
    para->read_data_fun = read_data_fun;
    s = pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    
    if(pthread_create (&ThreadId, &attr, (void *(*)(void *))istb_m3u8_thread, (void *)para))
    {
        m3u8DBG("create http server thread error \n");
        pthread_attr_destroy(&attr);
        return 0;
    }
    m3u8DBG("ThreadId:%ld \n",ThreadId);
    s = pthread_attr_destroy(&attr);
    return ThreadId;
}


int istb_m3u8_stop(pthread_t tid)
{
    int res;
    res = pthread_kill(tid, 0); //test is the pthread  alive
    m3u8DBG("thread  live status: %d \n",res);
    if (res ==3) {//ESRCH=3
        m3u8DBG("thread  has exit!\n");
        return 0;
    }
    terminate = 1; 
    while (1) {
        res = pthread_kill(tid, 0); //test is the pthread  alive
        if (res ==3) {//ESRCH=3
            break;
        }
        usleep(1000*10);//10ms
    }
    m3u8DBG("thread  stop finish \n");
    terminate=0; //reset teminate flag
    return 0;
}


m3u8buf_t * istb_m3u8_buffer_create(char * name){
    m3u8buf_t * m3u8buffer=(m3u8buf_t *)calloc(1,sizeof(m3u8buf_t));
    strncpy(m3u8buffer->bufname,name,strlen(name));
    m3u8DBG("new buffer(%p):%s \n",m3u8buffer,m3u8buffer->bufname);
    m3u8buffer->buf=(char *)calloc(M3U8_SEGMENT_BUFFER_SIZE,sizeof(char));
    m3u8buffer->bufsize=M3U8_SEGMENT_BUFFER_SIZE;
    m3u8buffer->read_ptr=m3u8buffer->write_ptr=0;
    return m3u8buffer;
}

int istb_m3u8_buffer_write(m3u8buf_t * m3u8buffer,char *writebuf,int len){
    //int res=0;
    if (!m3u8buffer || !writebuf) {
        m3u8DBG("param error \n");
        return -1;
    }
    //m3u8DBG("len=%d  \n",len);
    if(m3u8buffer->bufsize >(m3u8buffer->write_ptr+len)){
        memcpy((void *)m3u8buffer->buf+m3u8buffer->write_ptr,(void *)writebuf,len);
        m3u8buffer->write_ptr+=len;
    }
    else{
        //m3u8DBG("dddddd \n");
        m3u8buffer->bufsize+=((len<M3U8_SEGMENT_BUFFER_SIZE)?M3U8_SEGMENT_BUFFER_SIZE:(M3U8_SEGMENT_BUFFER_SIZE+len));
        m3u8buffer->buf=(char *)realloc( m3u8buffer->buf,m3u8buffer->bufsize);
        if (!m3u8buffer->buf) {
            m3u8DBG("realloc  error \n");
            return -1;
        }
        memcpy(m3u8buffer->buf+m3u8buffer->write_ptr,writebuf,len);
        m3u8buffer->write_ptr+=len;
    }
    return len; 
}


int istb_m3u8_buffer_read(m3u8buf_t * m3u8buffer,char *readbuf,int len){
    //int res=0;
    int readlen=len;
    if (!m3u8buffer || !readbuf) {
        m3u8DBG("param error \n");
        return -1;
    }
    m3u8DBG("buffer name=%s \n",m3u8buffer->bufname);
    if((m3u8buffer->read_ptr+readlen)> m3u8buffer->write_ptr ){
        readlen=m3u8buffer->write_ptr-m3u8buffer->read_ptr;
    }
    memcpy(readbuf,m3u8buffer->buf+m3u8buffer->read_ptr,readlen);
    m3u8buffer->read_ptr+=readlen;
    return readlen; 
}

int istb_m3u8_buffer_destroy(m3u8buf_t * m3u8buffer){
    if (m3u8buffer) {
        if (m3u8buffer->buf) {
            free(m3u8buffer->buf);
            m3u8buffer->buf=NULL;
        }
        free(m3u8buffer);
        m3u8buffer=NULL;
    }
    return 0; 
}
int istb_m3u8_queue_head_update(m3u8buf_t * m3u8buf)
{
    //m3u8DBG("m3u8buf:%s \n",m3u8buf->buf);
    if (!queue_head->m3u8data) {
        queue_head->m3u8data=m3u8buf;
    }
    else{
       //m3u8DBG("queue head m3u8buf point:%p \n",queue_head->m3u8data);
       istb_m3u8_buffer_destroy(queue_head->m3u8data);
       queue_head->m3u8data=m3u8buf;
    }   
    return 0;
}

void istb_m3u8_queue_init(void){
    queue_head=calloc(1,sizeof(m3u8queue_t));
    queue_head->m3u8data=NULL;
    ISTB_LINKED_LIST_INIT(&queue_head->link);
    return ;
}

int istb_m3u8_queue_add(m3u8buf_t * m3u8buffer){
    m3u8queue_t * queue_node=calloc(1,sizeof(m3u8queue_t));
    queue_node->m3u8data=m3u8buffer;
    ISTB_LINKED_LIST_ADD_TO_TAIL(&queue_head->link,&queue_node->link);
    return 0;
}

int istb_m3u8_queue_remove(void){
    //remove the first buffer every  time
    struct list_node  *m3u8index_node=queue_head->link.next;
    //notice:ISTB_LINKED_LIST_ENTRY:input must be m3u8index_node ,not queue_head->link.next  which can cause memory double link !!! 
    m3u8queue_t  * outdate_node =ISTB_LINKED_LIST_ENTRY(m3u8index_node,m3u8queue_t,link);
    ISTB_LINKED_LIST_REMOVE(m3u8index_node);
    if(outdate_node->m3u8data){
        m3u8DBG("yes the remove name=%s,fill=%d k \n",outdate_node->m3u8data->bufname,
                (outdate_node->m3u8data->write_ptr)/1024);
        istb_m3u8_buffer_destroy(outdate_node->m3u8data);
    }
    free(outdate_node);
    outdate_node=NULL;
    return 0;
}

int istb_m3u8_queue_destroy(m3u8queue_t * qhead){
    struct list_node  *listnode=NULL;
    struct list_node  *tmp=NULL;
    m3u8queue_t  *  queuenode=NULL;
    if (qhead != NULL) {
        ISTB_LINKED_LIST_FOREACH(&(qhead->link), listnode, tmp) {
            queuenode=ISTB_LINKED_LIST_ENTRY(listnode, m3u8queue_t,link);
            m3u8DBG("queuenode=%p \n",queuenode);
            if(queuenode->m3u8data){
                m3u8DBG("yes the destory name=%s fill=%d k \n",queuenode->m3u8data->bufname,
                        (queuenode->m3u8data->write_ptr)/1024);
                istb_m3u8_buffer_destroy(queuenode->m3u8data);
            }
            free(queuenode);
            queuenode=NULL;
        }
    }
    m3u8DBG("m3u8:\n %s \n",qhead->m3u8data->buf);
    istb_m3u8_buffer_destroy(qhead->m3u8data);
    free(qhead);
    qhead=NULL;

    return 0;
}


int istb_m3u8_segment_get(char *filename,char * readbuf, int readlen){
    m3u8DBG("name=%s \n",filename);
    struct list_node  *listnode=NULL;
    struct list_node  *tmp=NULL;
    m3u8queue_t  *  queuenode=NULL;
    int ret=0;
    char *ptr=NULL;
    char  c = '.'; 
    char suffixname[32]={0};

    if (queue_head==NULL) {
        m3u8DBG("queue head null \n");
        return -1;
    }
    ptr = strrchr(filename, c); //get the last c position
    strncpy(suffixname,ptr+1,strlen(ptr+1));
    m3u8DBG("suffixname=%s \n",suffixname);

    if(!strncmp(suffixname,"m3u8",strlen("m3u8"))){
        ret=istb_m3u8_buffer_read(queue_head->m3u8data,readbuf,readlen);
        return ret;
    }
    if (queue_head != NULL) {
        ISTB_LINKED_LIST_FOREACH(&(queue_head->link), listnode, tmp) {
            queuenode=ISTB_LINKED_LIST_ENTRY(listnode, m3u8queue_t,link);
            if(queuenode->m3u8data){
                m3u8DBG("yes the destory name=%s \n",queuenode->m3u8data->bufname);
                if(!strncmp(filename,queuenode->m3u8data->bufname,strlen(filename))){
                    ret=istb_m3u8_buffer_read(queuenode->m3u8data,readbuf,readlen);
                    return ret;
                }
            }
        }
    }
    return  -1;
}
#if 0
typedef void *  (*MG_FIFO_OPEN) (char *fifoname);
typedef int   (*MG_FIFO_READ) (void *fifohandle,char *buf, int readlen );
typedef int   (*MG_FIFO_WRITE) (void *fifohandle,char *buf, int writelen );
typedef int   (*MG_FIFO_SEEK) (void *fifohandle, int offset,int fromwhere );
typedef int   (*MG_FIFO_CLOSE) (void *fifohandle );
#endif


void * istb_m3u8_api_open(char *fifoname,int  * fifolen){
    m3u8DBG("name=%s \n",fifoname);
    struct list_node  *listnode=NULL;
    struct list_node  *tmp=NULL;
    m3u8queue_t  *  queuenode=NULL;
    int ret=0;
    char *ptr=NULL;
    char  c = '.'; 
    char suffixname[32]={0};

    if (queue_head==NULL) {
        m3u8DBG("queue head null \n");
        return -1;
    }
    ptr = strrchr(fifoname, c); //get the last c position
    strncpy(suffixname,ptr+1,strlen(ptr+1));
    m3u8DBG("suffixname=%s \n",suffixname);

    if(!strncmp(suffixname,"m3u8",strlen("m3u8"))){
        *fifolen=queue_head->m3u8data->write_ptr;
        return (void *)(queue_head->m3u8data);
    }
    if (queue_head != NULL) {
        ISTB_LINKED_LIST_FOREACH(&(queue_head->link), listnode, tmp) {
            queuenode=ISTB_LINKED_LIST_ENTRY(listnode, m3u8queue_t,link);
            if(queuenode->m3u8data){
                m3u8DBG("yes the destory name=%s \n",queuenode->m3u8data->bufname);
                if(!strncmp(fifoname,queuenode->m3u8data->bufname,strlen(fifoname))){
                     *fifolen=queuenode->m3u8data->write_ptr;
                    return  (void *)(queuenode->m3u8data);
                }
            }
        }
    }
    m3u8DBG("never find target file! \n");
    return 0;
}

//fix me :must add the lock!!!!!
int istb_m3u8_api_read(void *fifohandle,char *buf, int readlen ){
    int ret=0;
    if (!fifohandle||!buf) {
        m3u8DBG("param error \n");
        return -1;
    }
    m3u8buf_t *bufhandle=(m3u8buf_t *)fifohandle;
    ret=istb_m3u8_buffer_read(bufhandle,buf,readlen);
    return ret;
}

int istb_m3u8_api_write(void *fifohandle,char *buf, int writelen ){
    int ret=0;
    if (!fifohandle||!buf) {
        m3u8DBG("param error \n");
    }
    m3u8buf_t *bufhandle=(m3u8buf_t *)fifohandle;
    ret=istb_m3u8_buffer_write(bufhandle,buf,writelen);
    return ret;
}

int istb_m3u8_api_seek (void *fifohandle, int offset,int fromwhere){
    if (!fifohandle) {
        m3u8DBG("param error \n");
    }
    m3u8buf_t *bufhandle=(m3u8buf_t *)fifohandle;
    bufhandle->read_ptr=offset;
    return 0;
}
int istb_m3u8_api_close(void *fifohandle){
    return 0;
}
