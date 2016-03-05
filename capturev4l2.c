#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <netinet/in.h>
#include <netdb.h> 
#include <signal.h>
#include <getopt.h> 

#define CLEAR(x) memset(&(x), 0, sizeof(x))



struct buffer {
		void   *start;
		size_t  length;
};

unsigned int 	 n_buffers;  
struct buffer   *buffers;
uint32_t 		 buf_len;
int  			 camerafd = -1;
int 			 sockfd = -1;
struct sockaddr_in serv_addr;
char 			*camera_device;
int 			 width = 640;
int 			 height = 480;
char 			*frame_fmt;
int              frame_count = 100;

void ctrlC(int sig){ // can be called asynchronously
		int i;
		for(i = 0; i < n_buffers; i++){
				if (-1 == munmap(buffers[i].start, buffers[i].length))
						fprintf(stderr,"munmap error!\n");
		}
		if (-1 == close(camerafd))
				fprintf(stderr,"close");
		printf("ByeBye!!\n");
		exit(0);
}
static void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

static int xioctl(int fd, int request, void *arg)
{
		int r;

		do r = ioctl (fd, request, arg);
		while (-1 == r && EINTR == errno);

		return r;
}

int print_caps(int fd, int sockfd)
{
		struct v4l2_capability caps = {};
		if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &caps))
		{
				perror("Querying Capabilities");
				return 1;
		}

		printf( "Driver Caps:\n"
						"  Driver: \"%s\"\n"
						"  Card: \"%s\"\n"
						"  Bus: \"%s\"\n"
						"  Version: %d.%d\n"
						"  Capabilities: %08x\n",
						caps.driver,
						caps.card,
						caps.bus_info,
						(caps.version>>16)&&0xff,
						(caps.version>>24)&&0xff,
						caps.capabilities);


		struct v4l2_cropcap cropcap = {0};
		cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl (fd, VIDIOC_CROPCAP, &cropcap))
		{
				perror("Querying Cropping Capabilities");
				return 1;
		}

		printf( "Camera Cropping:\n"
						"  Bounds: %dx%d+%d+%d\n"
						"  Default: %dx%d+%d+%d\n"
						"  Aspect: %d/%d\n",
						cropcap.bounds.width, cropcap.bounds.height, cropcap.bounds.left, cropcap.bounds.top,
						cropcap.defrect.width, cropcap.defrect.height, cropcap.defrect.left, cropcap.defrect.top,
						cropcap.pixelaspect.numerator, cropcap.pixelaspect.denominator);

		int support_grbg10 = 0;

		struct v4l2_fmtdesc fmtdesc = {0};
		fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		char fourcc[5] = {0};
		char c, e;
		printf("  FMT : CE Desc\n--------------------\n");
		while (0 == xioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc))
		{
				strncpy(fourcc, (char *)&fmtdesc.pixelformat, 4);
				if (fmtdesc.pixelformat == V4L2_PIX_FMT_SGRBG10)
						support_grbg10 = 1;
				c = fmtdesc.flags & 1? 'C' : ' ';
				e = fmtdesc.flags & 2? 'E' : ' ';
				printf("  %s: %c%c %s\n", fourcc, c, e, fmtdesc.description);
				fmtdesc.index++;
		}
		/*
		   if (!support_grbg10)
		   {
		   printf("Doesn't support GRBG10.\n");
		   return 1;
		   }*/

		struct v4l2_format fmt = {0};
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		fmt.fmt.pix.width = width;
		fmt.fmt.pix.height = height;
		//fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_BGR24;
		//fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
		if(strncmp(frame_fmt,"MJPG",4) == 0){
			fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		}else if(strncmp(frame_fmt,"H264",4) == 0){
			fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
		}else{
			fprintf(stderr,"formate error!\n");
		}
		
		fmt.fmt.pix.field = V4L2_FIELD_NONE;

		if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
		{
				perror("Setting Pixel Format");
				return 1;
		}
		int n;
		strncpy(fourcc, (char *)&fmt.fmt.pix.pixelformat, 4);
		printf( "Selected Camera Mode:\n"
						"  Width: %d\n"
						"  Height: %d\n"
						"  PixFmt: %s\n"
						"  Field: %d\n",
						fmt.fmt.pix.width,
						fmt.fmt.pix.height,
						fourcc,
						fmt.fmt.pix.field);
		n = write(sockfd,fourcc,5);
		if (n < 0) 
				printf("ERROR writing to socket");
		n = write(sockfd,(char*)(&(fmt.fmt.pix.width)),4);
		if (n < 0) 
				printf("ERROR writing to socket");
		n = write(sockfd,(char*)(&(fmt.fmt.pix.height)),4);
		if (n < 0) 
				printf("ERROR writing to socket");
		return 0;
}

int init_mmap(int fd, int sockfd)
{
		struct v4l2_requestbuffers req = {0};
		req.count = 4;
		req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req.memory = V4L2_MEMORY_MMAP;

		if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req))
		{
				perror("Requesting Buffer");
				return 1;
		}
		if (req.count < 2) {
				fprintf(stderr, "Insufficient buffer memory on %s\n",
								camera_device);
				exit(EXIT_FAILURE);
		}

		buffers = calloc(req.count, sizeof(*buffers));
		if (!buffers) {
				fprintf(stderr, "Out of memory\n");
				exit(EXIT_FAILURE);
		}
		struct v4l2_buffer buf;
		for(n_buffers = 0; n_buffers < req.count; ++n_buffers){


				CLEAR(buf);

				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;
				buf.index = n_buffers;

				if(-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
				{
						perror("Querying Buffer");
						return 1;
				}
				buffers[n_buffers].length = buf.length;


				buffers[n_buffers].start = mmap (NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
				buf_len = buf.length;
				printf("buffer%d => Length: %d Address: %p\n", n_buffers, buf.length, buffers[n_buffers].start);
				// printf("Image Length: %d\n", buf.bytesused);
		}



		int n = write(sockfd,(char*)(&(buf.length)),4);
		if (n < 0) 
				printf("ERROR writing to socket");

		return 0;
}
int capture_and_send_image(int fd, int sockfd)
{

		enum v4l2_buf_type type;
		char camActive = 0;
		char rec;
		struct v4l2_buffer buf = {0};
		int n,i;
		char *bufferptr;
		int framesize;
		long long start, end;


		for (i = 0; i < n_buffers; ++i) {
				struct v4l2_buffer buf;

				CLEAR(buf);
				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;
				buf.index = i;

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
						printf("VIDIOC_QBUF");
		}

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
				printf("VIDIOC_STREAMON");

		while(1){

				fd_set fds;
				struct timeval tv;
				int r;

				FD_ZERO(&fds);
				FD_SET(fd, &fds);

				tv.tv_sec = 2;
				tv.tv_usec = 0;

				r = select(fd+1, &fds, NULL, NULL, &tv);
				if(-1 == r)
				{
						perror("Waiting for Frame");
						return 1;
				}
				CLEAR(buf);

				buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				buf.memory = V4L2_MEMORY_MMAP;

				if(-1 == xioctl(fd, VIDIOC_DQBUF, &buf))
				{
						switch (errno) {
								case EAGAIN:
										continue;

								case EIO:
										/* Could ignore EIO, see spec. */

										/* fall through */

								default:
										perror("VIDIOC_DQBUF");
						}
				}
				assert(buf.index < n_buffers);

				n = write(sockfd,(char*)&buf.bytesused,4);
				if (n < 0) {
						printf("ERROR writing to socket");
						ctrlC(0);
						return -1;
				}

				bufferptr = buffers[buf.index].start;
				framesize = buf.bytesused;
				while( (n = write(sockfd,bufferptr,framesize)) != framesize){
						if (n < 0){
								printf("ERROR writing to socket");
								ctrlC(0);
								return -1;
						}
						bufferptr += n;
						framesize -= n;
				}
				n = read(sockfd,&rec,1);

				if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
						printf("VIDIOC_QBUF");  

		}
		return 0;
}



int sock_init(struct hostent *server, int portno){
		int netfd = socket(AF_INET, SOCK_STREAM, 0);
		/* open socket and connect to server */
		if (netfd < 0) 
				printf("ERROR opening socket");
		if (server == NULL) {
				fprintf(stderr,"ERROR, no such host\n");
				exit(0);
		}
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr, 
						(char *)&serv_addr.sin_addr.s_addr,
						server->h_length);
		serv_addr.sin_port = htons(portno);
		if (connect(netfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
				printf("ERROR connecting to server\n");
				ctrlC(0);
				exit(-1);
		}
		return netfd;
}

static void usage(FILE *fp, int argc, char **argv)
{
        fprintf(fp,
                 "Usage: %s [options]\n\n"
                 "Options:\n"
                 "-d | --device name   Video device name [%s]\n"
                 "-h | --help          Print this message\n"
                 "-f | --format        [H264] or [MJPG]\n"
		 		 "-x | --width         Frame width [640]\n"
		 		 "-y | --height        Frame height [480]\n"
		 		 "-p | --port          Server port [5000]\n"
		 		 "-i | --ip            Server ip [127.0.0.1]\n"
                 "-c | --count         Number of frames to grab [%i] - use 0 for infinite\n"
                 "\n"
		 "Example usage: capture -f h264 -x 1280 -y 720 -c 300\n"
		 "Captures 300 frames of H264 at 1280x720\n",
                 argv[0], camera_device, frame_count);
}

static const char short_options[] = "d:hf:x:y:p:i:c:";
static const struct option
long_options[] = {
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { "format", required_argument, NULL, 'f' },
        { "width",  required_argument, NULL, 'x' },
        { "height", required_argument, NULL, 'y' },
        { "port",   required_argument, NULL, 'p' },
        { "ip",     required_argument, NULL, 'i' },
        { "count",  required_argument, NULL, 'c' },
        { 0, 0, 0, 0 }
};

int main(int argc, char *argv[])
{
		camera_device = "/dev/video0";
		frame_fmt ="MJPG";
		int portno = 5000;
		int fd, n;
		
		struct hostent *server = gethostbyname("127.0.0.1");

        for (;;) {
                int idx;
                int c;

                c = getopt_long(argc, argv, short_options, long_options, &idx);

                if (-1 == c)
                        break;

                switch (c) {
	                case 0: /* getopt_long() flag */
	                        break;

	                case 'd':
	                        camera_device = optarg;
	                        break;

	                case 'h':
	                        usage(stdout, argc, argv);
	                        exit(EXIT_SUCCESS);

	                case 'f':
	                        frame_fmt = optarg;
	                        break;
	                case 'x':
	                        errno = 0;
	                        width = strtol(optarg, NULL, 0);
	                        if (errno)
	                                errno_exit(optarg);
	                        break;
	                case 'y':
	                        errno = 0;
	                        height = strtol(optarg, NULL, 0);
	                        if (errno)
	                                errno_exit(optarg);
	                        break;
	                case 'p':
	                        errno = 0;
	                        portno = strtol(optarg, NULL, 0);
	                        if (errno)
	                                errno_exit(optarg);
	                        break;	  
	                case 'i':
	                        errno = 0;
	                        server = gethostbyname(optarg);
	                        if (errno)
	                                errno_exit(optarg);
	                        break;	
	                case 'c':
	                        errno = 0;
	                        frame_count = strtol(optarg, NULL, 0);
	                        if (errno)
	                                errno_exit(optarg);
	                        break;

	                default:
	                        usage(stderr, argc, argv);
	                        exit(EXIT_FAILURE);
                }
        }		


		sockfd = sock_init(server, portno);


		/* open camera and initialize */
		fd = open(camera_device, O_RDWR);
		if (fd == -1)
		{
				perror("Opening video device");
				return 1;
		}
		camerafd = fd;
		if(print_caps(fd, sockfd))
				return 1;

		if(init_mmap(fd, sockfd))
				return 1;

		signal(SIGINT, ctrlC);
		/*send image */
		capture_and_send_image(fd, sockfd);


		close(fd);
		return 0;
}
