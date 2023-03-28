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

#include <libavcodec/avcodec.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

#define INBUF_SIZE 4096

// float sum_encode = 0;
// float avg_encode = 0;
// float sum_decode = 0;
// float avg_decode = 0;
int frame_buff_size = 10*3*480*640;


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

static char* encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;
    char* buff;
    // struct timeval t1, t2;
    // double elapsedTime;
    buff = (char*)malloc(frame_buff_size*sizeof(char));

    /* send the frame to the encoder */
    // if (frame)
    //     printf("Send frame %3"PRId64"\n", frame->pts);
    
    // gettimeofday(&t1, NULL);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return NULL;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }
        // gettimeofday(&t2, NULL);
        // elapsedTime = (t2.tv_sec - t1.tv_sec)*1000.0;
        // elapsedTime = (t2.tv_usec - t1.tv_usec)/1000.0;

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);

        // printf("Encoding time for %1"PRId64" %f\n", pkt->pts, elapsedTime);
        // if (elapsedTime > 0)
        //     sum_encode += elapsedTime;
        memcpy(buff, pkt->data, pkt->size);
        fwrite(pkt->data, 1, pkt->size, outfile);
        
        return buff;
    }
}

static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                   const char *filename)
{
    char buf[1024];
    int ret;

    // struct timeval t1, t2;
    // double elapsedTime;

    // gettimeofday(&t1, NULL);
    
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

        // gettimeofday(&t2, NULL);
        // elapsedTime = (t2.tv_sec - t1.tv_sec)*1000.0;
        // elapsedTime = (t2.tv_usec - t1.tv_usec)/1000.0;
        printf("saving frame %3"PRId64"\n", dec_ctx->frame_num);
        // printf("Decoding frame %3"PRId64" time: %f\n", dec_ctx->frame_num, elapsedTime);
        fflush(stdout);
        // if (elapsedTime > 0)
        //     sum_decode += elapsedTime;

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%s-%"PRId64, filename, dec_ctx->frame_num);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}

int main(int argc, char** argv) {

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

    FILE *istream;
    char* img_file_path;
    int size;
    char* img;

    int data_size;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];


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
    d_codec = avcodec_find_decoder(AV_CODEC_ID_MPEG1VIDEO);
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

    memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    /* put sample parameters */
    c->bit_rate = 400000;
    /* resolution must be a multiple of two */
    c->width = 640;
    c->height = 480;
    /* frames per second */
    c->time_base = (AVRational){1, 25};
    c->framerate = (AVRational){25, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

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

    size = 60*3*480*640;
    img = (char*)malloc(size*sizeof(char));
    img_file_path = "/home/dhruva/my_sample/raw_video.yuv";
    istream = fopen(img_file_path, "r");
    fread(img, sizeof(char), size, istream);

    /* encode 2 second of video */
    for (i = 0; i < 50; i++) {
        fflush(stdout);

        /* Make sure the frame data is writable.
           On the first round, the frame is fresh from av_frame_get_buffer()
           and therefore we know it is writable.
           But on the next rounds, encode() will have called
           avcodec_send_frame(), and the codec may have kept a reference to
           the frame in its internal structures, that makes the frame
           unwritable.
           av_frame_make_writable() checks that and allocates a new buffer
           for the frame only if necessary.
         */
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* Y */
        int offset;
        offset = i*(480*640*1.5);

        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = img[y * frame->linesize[0] + x + offset];
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = img[y * frame->linesize[1] + x + offset];
                frame->data[2][y * frame->linesize[2] + x] = img[y * frame->linesize[2] + x + offset];
            }
        }

        frame->pts = i;

        /* encode the image */
        buff = encode(c, frame, pkt, f);
        data_size = pkt->size;

        /* Decode the compressed packets*/
        do
        {
            data_size -= dec_ret;
            dec_ret = av_parser_parse2(parser, d, &d_pkt->data, &d_pkt->size,
                                buff, pkt->size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        
            if (dec_ret < 0) {
             fprintf(stderr, "Error while parsing\n");
             exit(1);
            }
            
            if (d_pkt->size) {
                decode(d, d_frame, d_pkt, outdir);
            }
            
        }while (data_size);
        
        av_packet_unref(pkt);
    }
    
    fclose(istream);
    free(img);

    /* flush the encoder */
    encode(c, NULL, pkt, f);
    av_packet_unref(pkt);

    /* flush the decoder */
    decode(d, d_frame, NULL, outdir);

    av_parser_close(parser);
    avcodec_free_context(&d);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_frame_free(&d_frame);
    av_packet_free(&pkt);
    av_packet_free(&d_pkt);

}