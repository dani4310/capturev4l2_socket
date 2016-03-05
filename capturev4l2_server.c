#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include "cv.h"
#include "cxcore.h"
#include "highgui.h"
#include <sys/time.h>
#include <unistd.h>// sleep(3);
#include <sys/timeb.h>//timeb
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>
// #include <iostream>
// #include <sys/wait.h>
// using namespace cv;
// using namespace std;
#define LIGHT_RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define CLOSE_COLOR "\033[0m"


#define FORMATE_MJPG 1
#define FORMATE_H264 2

int width, height;
#ifdef FACEDETECT
	CvMemStorage* facesMemStorage;
	CvHaarClassifierCascade* classifier;
#endif

typedef struct SwsContext SwsContext;
struct FFMPEG
{
    AVCodec* codec ;
    AVCodecContext* context ;
    AVFrame* frame_yuv ;
    SwsContext* sws_context ;
    AVFrame* frame_rgb ;
    AVPicture rgb;
} m_ffmpeg;

int min_face_height = 50;
int min_face_width = 50;
void error(const char *msg)
{
		perror(msg);
		exit(1);
}

long long getSystemTime() {
		struct timeb t;
		ftime(&t);
		return 1000 * t.time + t.millitm;
}
void PrintFrameMsg(int current,double fps,long long speed, char *fourcc){
	int i;
	printf("\r");
	for(i = 0; i<current%3; i++){
		printf("\t\t\t\t\t");
	}
	printf("[camera%d "LIGHT_RED"%s"CLOSE_COLOR YELLOW" %.3f"CLOSE_COLOR"FPS "YELLOW"%4lld"CLOSE_COLOR"KB/s]",current, fourcc, fps, speed);
	for(i = 0; i<3 - current%3; i++){
		printf("\t\t\t\t\t");
	}
	fflush(stdout);
}

void ffmpeg_init(int width, int height, int formate){
   av_register_all();
   avcodec_register_all();
   avformat_network_init();
   if(formate == FORMATE_H264){
	   m_ffmpeg.codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	}else if(formate == FORMATE_MJPG){ 
	   m_ffmpeg.codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
	}
   if (!m_ffmpeg.codec) 
   {
      fprintf(stderr, "Codec not found\n");
      exit(1);
   }
   m_ffmpeg.context = avcodec_alloc_context3(m_ffmpeg.codec);
   if (!m_ffmpeg.context)
   {
      fprintf(stderr, "Could not allocate video codec context\n");
      exit(1);
   }
   avcodec_get_context_defaults3(m_ffmpeg.context, m_ffmpeg.codec);
   // m_ffmpeg.context->flags |= CODEC_FLAG_LOW_DELAY;
   m_ffmpeg.context->flags2 |= CODEC_FLAG2_CHUNKS;
   m_ffmpeg.context->thread_count = 4;
   m_ffmpeg.context->thread_type = FF_THREAD_FRAME;
   m_ffmpeg.context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
   if (avcodec_open2(m_ffmpeg.context, m_ffmpeg.codec, NULL) < 0)
   {
      fprintf(stderr, "Could not open codec\n");
      exit(1);
   }
   
   m_ffmpeg.frame_yuv = av_frame_alloc();
   if (!m_ffmpeg.frame_yuv) 
   {
      fprintf(stderr, "Could not allocate video frame\n");
      exit(1);
   }
   m_ffmpeg.frame_rgb = av_frame_alloc();
   if (!m_ffmpeg.frame_rgb) 
   {
      fprintf(stderr, "Could not allocate video frame\n");
      exit(1);
   }
    uint8_t *buffer;
    buffer = malloc(avpicture_get_size(PIX_FMT_RGB24, width, height));
    avpicture_fill((AVPicture *)(m_ffmpeg.frame_rgb), buffer, PIX_FMT_RGB24, width, height); 

}
#ifdef FACEDETECT
IplImage *facedetect(IplImage* image_detect){
		int i;


		IplImage* frame=cvCreateImage(cvSize(image_detect->width, image_detect->height), IPL_DEPTH_8U, image_detect->nChannels);
		if(image_detect->origin==IPL_ORIGIN_TL){
				cvCopy(image_detect, frame, 0);    }
		else{
				cvFlip(image_detect, frame, 0);    }
		cvClearMemStorage(facesMemStorage);
		CvSeq* faces=cvHaarDetectObjects(frame, classifier, facesMemStorage, 1.1, 3, CV_HAAR_DO_CANNY_PRUNING, cvSize(min_face_width, min_face_height), cvSize(0,0));
		if(faces){
				for(i=0; i<faces->total; ++i){
						// Setup two points that define the extremes of the rectangle,
						// then draw it to the image
						CvPoint point1, point2;
						CvRect* rectangle = (CvRect*)cvGetSeqElem(faces, i);
						point1.x = rectangle->x;
						point2.x = rectangle->x + rectangle->width;
						point1.y = rectangle->y;
						point2.y = rectangle->y + rectangle->height;
						cvRectangle(frame, point1, point2, CV_RGB(255,0,0), 3, 8, 0);
				}
		}
		cvReleaseImage(&image_detect);
		return frame;
}
#endif
static void process_image(void *p, int size, char* fourcc)
{
	static first_time = 1;
	    IplImage *cpimgrgb;
	    AVPacket packet;
	    av_init_packet(&packet);
	    packet.pts = AV_NOPTS_VALUE;
	    packet.dts = AV_NOPTS_VALUE;
	    packet.data = p;//your frame data
	    packet.size = size;//your frame data size
	    int got_frame = 0;
	    int len = avcodec_decode_video2(m_ffmpeg.context, m_ffmpeg.frame_yuv, &got_frame, &packet);
	    if (len >= 0 && got_frame)
	    {
	    	if(first_time){
		        m_ffmpeg.sws_context = sws_getContext(width, height, m_ffmpeg.context->pix_fmt, width, height, PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);   
		        first_time = 0;
	        }
	        sws_scale(m_ffmpeg.sws_context, (const uint8_t* const*)m_ffmpeg.frame_yuv->data, m_ffmpeg.frame_yuv->linesize, 0, height, m_ffmpeg.frame_rgb->data, m_ffmpeg.frame_rgb->linesize);

	        cpimgrgb = cvCreateImage(cvSize(width,height), IPL_DEPTH_8U, 3);
	        cpimgrgb->imageSize = width*height*3;

	        cpimgrgb->imageData = (char*)(m_ffmpeg.frame_rgb->data[0]);
	        cvCvtColor(cpimgrgb, cpimgrgb, 4);
#ifdef FACEDETECT
	        cpimgrgb =facedetect(cpimgrgb);
#endif
	        cvNamedWindow(fourcc,CV_WINDOW_AUTOSIZE);
	        cvShowImage(fourcc, cpimgrgb);
	        cvWaitKey(1);
	        cvReleaseImage(&cpimgrgb); 
	        // sws_freeContext(m_ffmpeg.sws_context);
	    }  else{
	        fprintf(stderr, "avcodec errror!!\n");
	    } 
	    av_free_packet(&packet);
               
}



int main(int argc, char *argv[])
{   
		int sockfd, newsockfd, portno, childpid;
		socklen_t clilen;
		char messagerec[30] = "";
		char *mesptr = messagerec;
		struct sockaddr_in serv_addr, cli_addr;
		int n;
		char killbuf[20];
		long long start, end;
		int current = 0;
		char fourcc[5];
		uint32_t formate_type =0;


		// startWindowThread();
		if (argc < 2) {
				printf("Usage: ./CVserver <server port>\n");
				exit(1);
		}
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) 
				error("ERROR opening socket");
		bzero((char *) &serv_addr, sizeof(serv_addr));
		portno = atoi(argv[1]);
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(portno);
		if (bind(sockfd, (struct sockaddr *) &serv_addr,
								sizeof(serv_addr)) < 0) 
				error("ERROR on binding");
		listen(sockfd,5);
		fflush(stdout);
		for(;;){
				clilen = sizeof(cli_addr);
				newsockfd = accept(sockfd, 
								(struct sockaddr *) &cli_addr, 
								&clilen);
				if (newsockfd < 0) 
						error("ERROR on accept");
				if((childpid = fork()) < 0)
						error("server:fork error");
				else if(childpid == 0){//child
						uint32_t framesize, framesize_count, frame_totalsize;
						uint32_t buffersize;
						if(read(newsockfd, fourcc, 5) == 0){
								PrintFrameMsg(current, 0, 0, fourcc);
								sprintf(killbuf,"kill %d",(int)getpid());
								n = system((const char*)killbuf);
								exit(0);
						}
						if(read(newsockfd, (char*) &width, 4) == 0){
								PrintFrameMsg(current, 0, 0, fourcc);
								sprintf(killbuf,"kill %d",(int)getpid());
								n = system((const char*)killbuf);
								exit(0);
						}
						if(read(newsockfd, (char*) &height, 4)==0){
								PrintFrameMsg(current, 0, 0, fourcc);
								sprintf(killbuf,"kill %d",(int)getpid());
								n = system((const char*)killbuf);
								exit(0);
						}
						if(strncmp(fourcc,"H264",4) == 0){
							fourcc[4] = '\0';
							formate_type = FORMATE_H264;
						}else if(strncmp(fourcc,"MJPG",4) == 0){
							fourcc[4] = '\0';
							formate_type = FORMATE_MJPG;
						}else{
							fprintf(stderr,"Image formate error! message:[%s]\n",fourcc);
							exit(-1);
						}
						ffmpeg_init(width, height, formate_type);

						if(read(newsockfd, (char*) &buffersize, 4) == 0){
								PrintFrameMsg(current, 0, 0, fourcc);
								sprintf(killbuf,"kill %d",(int)getpid());
								n = system((const char*)killbuf);
								exit(0);
						}

						char buffer[buffersize];
						char *bufferptr;
#ifdef FACEDETECT
						//facedetection needed data
						char cascade_name[]="haarcascade_frontalface_alt.xml";
						// Load cascade
						classifier=(CvHaarClassifierCascade*)cvLoad(cascade_name, 0, 0, 0);
						if(!classifier){
								fprintf(stderr,"ERROR: Could not load classifier cascade.");
								return -1;
						}
						facesMemStorage=cvCreateMemStorage(0);
#endif
						n = write(newsockfd,".....",5);
						int n,i,frame_num = 0;
						while(1){

								n = write(newsockfd,".",1);
								if(frame_num == 0){
										frame_totalsize = 0;
										start = getSystemTime();
								}
								if(read(newsockfd, (char*) &framesize, 4) == 0){
										PrintFrameMsg(current, 0, 0, fourcc);
										sprintf(killbuf,"kill %d",(int)getpid());
										n = system((const char*)killbuf);
										exit(0);
								}
								frame_totalsize += (uint32_t)framesize;
								framesize_count = framesize;
								bufferptr = buffer;
								while( (n = read(newsockfd, bufferptr, framesize_count)) != framesize_count){
										if(n == 0){
												PrintFrameMsg(current, 0, 0, fourcc);
												sprintf(killbuf,"kill %d",(int)getpid());
												n = system((const char*)killbuf);
												exit(0);
										}
										bufferptr += n;
										framesize_count -= n;
								}




								process_image(buffer, framesize, fourcc);

								frame_num++;
								if(frame_num == 20){
									end = getSystemTime();
									frame_num = 0;
									PrintFrameMsg(current, 20000.0/(end - start), frame_totalsize/(end - start), fourcc);
									// printf("\r\t\t\t\t\t\t\t\t\t\t\t%lldB/s",);
								}
						}
						close(newsockfd);
						close(sockfd);
						exit(0); 
				}
				//parent
				current++;
				close(newsockfd);



		}
}
