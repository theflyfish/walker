#CROSS=armv7-linux-

CC	=$(CROSS)gcc
AR	=$(CROSS)ar

CFLAGS=-c -g -Wall

TARGET_HTTP_LIB	= ilibhttpserver.a
TARGET_M3U8_LIB	= ilibm3u8.a
TARGET_UDP_LIB	= ilibudp.a
TARGET_RINGBUF_LIB = ilibringbuffer.a
TARGET_ELF = iUDPtoHLS

TARGETS = $(TARGET_HTTP_LIB) $(TARGET_M3U8_LIB) $(TARGET_UDP_LIB) $(TARGET_RINGBUF_LIB) $(TARGET_ELF)

HTTPSERVER_OBJS	= istb_httpserver.o  istb_mongoose.o
M3U8_OBJS       = istb_m3u8.o
UDP_OBJS        = istb_udp.o
RINGBUFFER_OBJS = istb_ringbuffer.o


default: $(TARGETS)

all: $(TARGETS)

$(TARGET_HTTP_LIB): $(HTTPSERVER_OBJS)
	$(AR) rcu $(TARGET_HTTP_LIB) $(HTTPSERVER_OBJS)

$(TARGET_UDP_LIB): $(UDP_OBJS)
	$(AR) rcu $(TARGET_UDP_LIB) $(UDP_OBJS)

$(TARGET_RINGBUF_LIB): $(RINGBUFFER_OBJS)
	$(AR) rcu $(TARGET_RINGBUF_LIB) $(RINGBUFFER_OBJS)

$(TARGET_M3U8_LIB): $(M3U8_OBJS)
	$(AR) rcu $(TARGET_M3U8_LIB) $(M3U8_OBJS) 

$(TARGET_ELF): $(TARGET_HTTP_LIB) $(TARGET_M3U8_LIB) $(TARGET_UDP_LIB)  $(TARGET_RINGBUF_LIB) main.o
	$(CC) main.o  $(TARGET_HTTP_LIB) $(TARGET_M3U8_LIB) $(TARGET_UDP_LIB)  $(TARGET_RINGBUF_LIB)  -o $(TARGET_ELF) -lpthread -L/usr/local/lib -lavformat -lavcodec -ldl -lasound -lSDL -lz -lrt -lavutil -lm 

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@ 

clean:
	rm -f *.o $(TARGETS)
