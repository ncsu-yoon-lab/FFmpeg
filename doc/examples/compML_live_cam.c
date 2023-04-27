/**
 * @file libavcodec encoding video API usage example
 * @example encode_video.c
 *
 * Read from a folder of images and encode it to an output file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libv4l2.h>
#include <signal.h>

#include <libavcodec/avcodec.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#define INBUF_SIZE 4096

struct timeval t1, t2;
// struct timeval t3, t4;
double elapsedTime;
int enc_frame_num = 0;
// double enc_time;

void ctrlc(void)
{
    exit(EXIT_FAILURE);
}

void xioctl(int fh, int request, void *arg)
{
    int r;
    do
    {
        r = v4l2_ioctl(fh, request, arg);
    } while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

    if (r == -1)
    {
        printf("[usbcam.h] USB request failed (%d): %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

#define usbcam_assert(CONDITION, MESSAGE) { if (!(CONDITION)) { printf("[usbcam.h] Error at line %d: %s\n", __LINE__, MESSAGE); exit(EXIT_FAILURE); } }


static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"wb");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    // gettimeofday(&t1, NULL);
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        
        return pkt;
    }
}

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                   const char *filename, SDL_Renderer* renderer, SDL_Texture* texture)
{
    char buf[1024];
    int ret;
    
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            exit(1);
        }

        printf("saving frame %3"PRId64"\n", dec_ctx->frame_num);
        // printf("Latency for frame %3"PRId64": %f\n", dec_ctx->frame_num, elapsedTime);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%s-%"PRId64, filename, dec_ctx->frame_num);
        /* Display the decoded images on the screen*/
        SDL_UpdateYUVTexture(
                    texture,
                    NULL,
                    frame->data[0],
                    dec_ctx->width,
                    frame->data[1],
                    frame->linesize[1],
                    frame->data[2],
                    frame->linesize[1]
                );
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);

        /* Uncomment below code to save images */
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}

int main(int argc, char** argv) {

    signal(SIGINT, ctrlc);

    const char *codec_name, *outfilename, *outdir; // codec, MP1 video, Image Dir
    const AVCodec *codec;   // Encoder codec
    const AVCodec *d_codec;   // Decoder codec
    AVCodecParserContext *parser;   // Parse video context
    AVCodecContext *c=NULL;
    AVCodecContext *d=NULL;
    int i, ret, x, y;
    int dec_ret;
    FILE *f;
    AVFrame *frame;
    AVFrame *d_frame;
    AVPacket *pkt;
    AVPacket *d_pkt;
    AVPacket *buff;

    size_t data_size;
    int eof;

    SDL_Surface* screen;
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    SDL_Surface* e_screen;
    SDL_Renderer *e_renderer;
    SDL_Texture *e_texture;

    char *device_name = "/dev/video0";
    int device_buffers = 3;
    int device_width = 640;
    int device_height = 480;
    int device_format = V4L2_PIX_FMT_YUYV;

    printf("Opening camera %s\n", device_name);

    // Open the device
    int fd = v4l2_open(device_name, O_RDWR, 0);
    usbcam_assert(fd >= 0, "Failed to open device");

    // set format
    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = device_format;
    fmt.fmt.pix.width = device_width;
    fmt.fmt.pix.height = device_height;
    xioctl(fd, VIDIOC_S_FMT, &fmt);
    
    usbcam_assert(fmt.fmt.pix.pixelformat == device_format, "Did not get the requested format");
    usbcam_assert(fmt.fmt.pix.width == device_width, "Did not get the requested width");
    usbcam_assert(fmt.fmt.pix.height == device_height, "Did not get the requested height");
    
    // tell the driver how many buffers we want
    struct v4l2_requestbuffers request = {0};
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    request.count = device_buffers;
    xioctl(fd, VIDIOC_REQBUFS, &request);
    usbcam_assert(request.count == device_buffers, "Did not get the requested number of buffers");

    // allocate buffer
    void *buffer_start[3] = {0};
    uint32_t buffer_length[3] = {0};
    for (int i = 0; i < device_buffers; i++)
    {
        struct v4l2_buffer info = {0};
        info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        info.memory = V4L2_MEMORY_MMAP;
        info.index = i;
        xioctl(fd, VIDIOC_QUERYBUF, &info);

        buffer_length[i] = info.length;
        buffer_start[i] = mmap(
            NULL,
            info.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            info.m.offset
        );

        usbcam_assert(buffer_start[i] != MAP_FAILED, "Failed to allocate memory for buffers");
    }

    if (SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        exit(1);
    }


    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <codec> <output video file> <output image dir>\n"
                "And check your input file is encoded by mpeg1video please.\n", argv[0]);
        exit(0);
    }

    codec_name = argv[1];
    outfilename = argv[2];
    outdir = argv[3];

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    /* find the MPEG-1 video decoder */
    d_codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!d_codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    parser = av_parser_init(d_codec->id);
    if (!parser) {
        fprintf(stderr, "parser not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate encoder codec context\n");
        exit(1);
    }

    d = avcodec_alloc_context3(d_codec);
    if (!d) {
        fprintf(stderr, "Could not allocate decoder codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    d_pkt = av_packet_alloc();
    if (!d_pkt) {
        fprintf(stderr, "No packets!");
        exit(1);
    }

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 640;
    c->height = 480;
    /* frames per second */
    c->time_base = (AVRational){1, 20};
    c->framerate = (AVRational){20, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV422P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(c->priv_data, "preset", "slow", 0);
        // av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    }

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open encoder codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* open it */
    if (avcodec_open2(d, d_codec, NULL) < 0) {
        fprintf(stderr, "Could not open decoder codec\n");
        exit(1);
    }

    f = fopen(outfilename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", outfilename);
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame->format = c->pix_fmt;
    frame->width  = c->width;
    frame->height = c->height;

    /* Make a screen to display video of decoded output and raw camera input */
    screen = SDL_CreateWindow(
            "CompML Decoded Display",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            c->width,
            c->height,
            0
        );
    if (!screen) {
        fprintf(stderr, "SDL could not create window for decoded ouput - exiting \n");
        exit(1);
    }

    e_screen = SDL_CreateWindow(
            "CompML Raw Image Stream Display",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            c->width,
            c->height,
            0
        );
    if (!e_screen) {
        fprintf(stderr, "SDL could not create window for raw image stream - exiting \n");
        exit(1);
    }

    renderer = SDL_CreateRenderer(screen, -1, 0);
    if (!renderer) {
        fprintf(stderr, "SDL: could not create renderer for decoded ouput- exiting\n");
        exit(1);
    }

    e_renderer = SDL_CreateRenderer(e_screen, 0, 0);
    if (!e_renderer) {
        fprintf(stderr, "SDL: could not create renderer for raw image stream - exiting\n");
        exit(1);
    }

    // Allocate a place to put our YUV image on that screen
    texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING,
            c->width,
            c->height
        );
    if (!texture) {
        fprintf(stderr, "SDL: could not create texture - exiting\n");
        exit(1);
    }

    e_texture = SDL_CreateTexture(
            e_renderer,
            SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING,
            c->width,
            c->height
        );
    if (!e_texture) {
        fprintf(stderr, "SDL: could not create texture - exiting\n");
        exit(1);
    }

    d_frame = av_frame_alloc();
    if (!d_frame) {
        fprintf(stderr, "Could not allocate video frame for decoder\n");
        exit(1);
    }

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

     // start streaming
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd, VIDIOC_STREAMON, &type);

    // queue buffers
    for (int p = 0; p < device_buffers; p++)
    {
        struct v4l2_buffer info = {0};
        info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        info.memory = V4L2_MEMORY_MMAP;
        info.index = p;
        xioctl(fd, VIDIOC_QBUF, &info);
    }

    int y_ele = (c->width)*(c->height);

    /* stream from camera */
    for (i = 0; i < 100; i++) {
        fflush(stdout);
        
        /* Get latest data from camera*/
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        // get a buffer
        xioctl(fd, VIDIOC_DQBUF, &buf);
        // check if there are more buffers available
        int r = 1;
        while (r == 1)
        {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            struct timeval tv; // if both fields = 0, select returns immediately
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            r = select(fd + 1, &fds, NULL, NULL, &tv); // todo: what if r == -1?
            if (r == 1)
            {
                printf(".");
                // queue the previous buffer
                xioctl(fd, VIDIOC_QBUF, &buf);
                // get a new buffer
                xioctl(fd, VIDIOC_DQBUF, &buf);
            }
        }

        unsigned char *img = (unsigned char*)buffer_start[buf.index];
        unsigned int img_size = buf.bytesused;

        /* FFMpeg Encoding/Decoding and display on screen parts */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

       /* Y */
       x = 0;
        for (y = 0; y < y_ele; y++) {
            frame->data[0][x] = img[2*y];
            x++;  
        }

        x = 0;
        for (y = 0; y < y_ele/2; y++) {
            frame->data[1][x] = img[2*y + 1];
            frame->data[2][x] = img[2*y+ 3];
            x++;  
        }

        frame->pts = i;
        /* Display the raw images stream on the screen*/
        SDL_UpdateYUVTexture(
                    e_texture,
                    NULL,
                    frame->data[0],
                    c->width,
                    frame->data[1],
                    frame->linesize[1],
                    frame->data[2],
                    frame->linesize[1]
                );
        SDL_RenderClear(e_renderer);
        SDL_RenderCopy(e_renderer, e_texture, NULL, NULL);
        SDL_RenderPresent(e_renderer);

        /* encode the image */
        gettimeofday(&t1, NULL);
        encode(c, frame, pkt, f);
        if (pkt->pts == frame->pts) {
              gettimeofday(&t2, NULL);
              elapsedTime = (t2.tv_usec - t1.tv_usec)/1000.0;
              printf("Encode latency for frame %3"PRId64": %f\n", pkt->pts, elapsedTime);
        }
        data_size = pkt->size;

        /* Decode the compressed packets*/
        eof = !data_size;

         do {
            dec_ret = av_parser_parse2(parser, d, &d_pkt->data, &d_pkt->size,
                            pkt->data, pkt->size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            
            if (dec_ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }

            data_size -= dec_ret;
            
            if (d_pkt->size) {
                decode(d, d_frame, d_pkt, outdir, renderer, texture);
            }
            else if (eof)
                break;
        } while (data_size > 0 || eof);
            
        
        av_packet_unref(pkt);
        xioctl(fd, VIDIOC_QBUF, &buf);
    }
    
    // turn off streaming
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    close(fd);

    // dequeue buffers

    for (int i = 0; i < device_buffers; i++)
        munmap(buffer_start[i], buffer_length[i]);
     
    /* flush the decoder */
    decode(d, d_frame, NULL, outdir, renderer, texture);

    /* flush the encoder */
    encode(c, NULL, pkt, f);
    av_packet_unref(pkt);

    /* flush ffmpeg resources */
    av_parser_close(parser);
    avcodec_free_context(&d);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_frame_free(&d_frame);
    av_packet_free(&pkt);
    av_packet_free(&d_pkt);

    /* flush SDL resources*/
    SDL_DestroyTexture(texture);
    SDL_DestroyTexture(e_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyRenderer(e_renderer);
    SDL_DestroyWindow(screen);
    SDL_DestroyWindow(e_screen);
    SDL_Quit();
}
