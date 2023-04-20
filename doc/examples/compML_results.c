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
#include <libswscale/swscale.h>

int frame_buff_size = 10*320*160;
struct timeval t1, t2;
double elapsedTime;

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

char* encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
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

    free(buff);

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

        
        printf("Decoded frame %3"PRId64"\n", dec_ctx->frame_num);
        // printf("Latency %3"PRId64" time: %f\n", dec_ctx->frame_num, elapsedTime);
        fflush(stdout);

        /* Uncomment if you want to save raw images */
        // pgm_save(frame->data[0], frame->linesize[0],
        //          frame->width, frame->height, buf);
    }
}

int main(int argc, char** argv) {

    const char *codec_name, *outfilename, *outdir; // codec, MP1 video, Image Dir
    const AVCodec *codec;   // Encoder codec
    const AVCodec *d_codec;   // Decoder codec
    AVCodecParserContext *parser;   // Parse video context
    AVCodecContext *c=NULL;
    AVCodecContext *d=NULL;
    int ret, x, y;
    int i = 0;
    int dec_ret;
    FILE *f;
    AVFrame *frame;
    AVFrame *d_frame;
    AVPacket *pkt;
    AVPacket *d_pkt;
    AVPacket *buff;

    FILE *istream;
    FILE *csvstream;
    char* img_file_path;
    float size;
    unsigned char* img;

    int data_size;
    int eof;
    int eofile;
    size_t read_size;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <codec> <output video file> <output image dir>\n"
                "And check the codec assigned in the code for decoding!\n", argv[0]);
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
    c->bit_rate = 1000000;
    /* resolution must be a multiple of two */
    c->width = 320;
    c->height = 160;
    /* frames per second */
    c->time_base = (AVRational){1, 20};
    c->framerate = (AVRational){20, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    c->gop_size = 20;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

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

    size = 160*320*1.5;
    img = (unsigned char*)malloc(size*sizeof(unsigned char));
    img_file_path = "/home/dhruva/my_sample/2016-01-31--19-19-25_yuv.yuv";
    istream = fopen(img_file_path, "r");
    csvstream = fopen("2016-01-31--19-19-25_yuv_gop20_1000kbps.csv", "a+");

    int u_index = (c->width)*(c->height);
    int v_index = u_index + (c->width)*(c->height)/4;
    

    /* encode entire video */
    do {
        
        fflush(stdout);
        read_size = fread(img, sizeof(unsigned char), size, istream);
        if (ferror(istream))
            break;

        eofile = !read_size;
        
        ret = av_frame_make_writable(frame);
        if (ret < 0)
            exit(1);

        /* Y */
        for (y = 0; y < c->height; y++) {
            for (x = 0; x < c->width; x++) {
                frame->data[0][y * c->width + x] = img[y * c->width + x];
            }
        }

        /* Cb and Cr */
        for (y = 0; y < c->height/2; y++) {
            for (x = 0; x < c->width/2; x++) {
                frame->data[1][y * (c->width/2) + x] = img[u_index + y * (c->width/2) + x];
                frame->data[2][y * (c->width/2) + x] = img[v_index + y * (c->width/2) + x];
            }
        }

        frame->pts = i;
        i++;

        /* encode the image */
        gettimeofday(&t1, NULL);
        buff = encode(c, frame, pkt, f);
        data_size = pkt->size;

        /* Decode the compressed packets*/
        eof = !data_size;

         do {
            dec_ret = av_parser_parse2(parser, d, &d_pkt->data, &d_pkt->size,
                            buff, pkt->size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            
            // printf("Dec-ret: %d \n", dec_ret);
            
            if (dec_ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }
            // data += dec_ret;
            data_size -= dec_ret;
            // printf("New data size: %d \n", data_size);
            if (d_pkt->size) {
                decode(d, d_frame, d_pkt, outdir);
                gettimeofday(&t2, NULL);
                elapsedTime = (t2.tv_sec - t1.tv_sec)*1000.0;
                elapsedTime = (t2.tv_usec - t1.tv_usec)/1000.0;
                fprintf(csvstream, "%f \n", elapsedTime);
            }
            else if (eof)
                break;
        } while (data_size > 0 || eof);
        
        av_packet_unref(pkt);
    } while (!eofile);
    
    fclose(istream);
    free(img);
     /* flush the decoder */
    decode(d, d_frame, NULL, outdir);

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

}
