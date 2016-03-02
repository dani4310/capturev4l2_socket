CC = gcc
CCFLAGS = -O2 `pkg-config --cflags --libs opencv`
SCFLAGS = -O2 `pkg-config --cflags libswscale --libs libv4l2 opencv` -I/usr/local/include/libavformat -I/usr/local/include/libavcodec -I/usr/local/include/libavutil -L/usr/local/lib -lavformat -lavcodec -lavutil
.PHONY:clean
	
origin:capturev4l2 capturev4l2_server

face:capturev4l2 capturev4l2_server_face

local:capturev4l2_local.c
	$(CC) $< -o capturev4l2_local $(CCFLAGS)

capturev4l2:capturev4l2.c
	$(CC) $< -o $@ $(CCFLAGS)

capturev4l2_server:capturev4l2_server.c
	$(CC) $< -o $@ $(SCFLAGS)
	
capturev4l2_server_face:capturev4l2_server.c
	$(CC) $< -o $@ $(SCFLAGS) -D FACEDETECT

clean:
	rm -f capturev4l2 capturev4l2_server capturev4l2_local capturev4l2_server_face