
walker
======

1.This Project TO BE CONTINUE


2.FUNCTION: a system which will tranport the UDP ts data to m3u8 segment for HLS,then the httpserver manage those segments.


3.MODULES: 

    Httpserver module --manange the m3u8 data,request to the HLS request from client,this module refer to the opensource project "mongoose"

    M3U8 module       --transport data to m3u8 format,this module refers to the opensource project "m3u8-segmenter".

    Ringbuffer module --buffer the upd data for m3u8 modules

    UDP module        --get upd data for m3u8 tranporting


4.Feature:

  the system is very efficient,as it has an very brilliant feature: All the intput or output data  is stored in the Memory Buffer,never in files. the m3u8 modules create the segments in buffer instead of file format ,then the httpserver get the buffer manager handle,tranport the url file request to special buffer with an buffer-uri map .


5.Usage:

type 'make' to build UDPtoHLS, and type 'make clean' to clear the target.


you can use the vlc player as the udp source with a mpegts stream,then test the UDPtoHLS elf.


input param:(must do it)


-i:  input stream address  as: 127.0.0.1:1234 , used by udp modules

-o:  the output  address  for   hls segment streams,  used by  httpserver  modules, as: 192.168.1.15:8080

-n:  the maxnum of stream  segments  created by  m3u8 modules 

-d:  by second,the duration of every stream segment,used by m3u8 modules 

-p:  the prefix for segment name,such as: walk-1.ts\walk-2.ts\walk-3.ts, for the m3u8 index file 

-m:  the file name of m3u8 file 


for example:

./iUDPtoHLS -i 127.0.0.1:1234  -o  127.0.0.1:8909  -n 5 -d 10 -p walk -m walker.m3u8  

6.MAIL:yucareer@163.com
