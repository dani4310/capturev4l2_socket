all:
	gcc -O2 capturev4l2.c -o capturev4l2 `pkg-config --cflags --libs opencv`
	gcc -O2 capturev4l2_server.c -o capturev4l2_server `pkg-config --cflags libswscale --libs libv4l2 opencv` -I/usr/local/include/libavformat -I/usr/local/include/libavcodec -I/usr/local/include/libavutil -L/usr/local/lib -lavformat -lavcodec -lavutil

face:
	gcc -O2 capturev4l2.c -o capturev4l2 `pkg-config --cflags --libs opencv`
	gcc -O2 capturev4l2_server.c -o capturev4l2_server `pkg-config --cflags libswscale --libs libv4l2 opencv` -I/usr/local/include/libavformat -I/usr/local/include/libavcodec -I/usr/local/include/libavutil -L/usr/local/lib -lavformat -lavcodec -lavutil -D FACEDETECT

local:
	gcc -O2 capturev4l2_local.c -o capturev4l2_local `pkg-config --cflags --libs opencv`
