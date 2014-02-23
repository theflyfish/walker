
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



5.MAIL:yucareer@163.com
