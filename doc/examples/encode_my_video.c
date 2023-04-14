/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

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

float sum = 0;
float avg = 0;


static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,
                   FILE *outfile)
{
    int ret;
    struct timeval t1, t2;
    double elapsedTime;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);
    
    gettimeofday(&t1, NULL);

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
        gettimeofday(&t2, NULL);
        elapsedTime = (t2.tv_sec - t1.tv_sec)*1000.0;
        elapsedTime = (t2.tv_usec - t1.tv_usec)/1000.0;

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        // printf("Encoding time for %1"PRId64" %f\n", pkt->pts, elapsedTime);
        if (elapsedTime > 0)
            sum += elapsedTime;
        fwrite(pkt->data, 1, pkt->size, outfile);
        av_packet_unref(pkt);
    }
}

int main(int argc, char **argv)
{
    const char *filename, *codec_name;
    const AVCodec *codec;
    AVCodecContext *c= NULL;
    int ret, x, y;
    int i = 0;
    FILE *f;
    AVFrame *frame;
    AVPacket *pkt;
    uint8_t endcode[] = { 0, 0, 1, 0xb7 };

    FILE *istream;
    char* img_file_path;
    float size;
    unsigned char* img;

    int eofile;
    size_t read_size;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    filename = argv[1];
    codec_name = argv[2];

    /* find the mpeg1video encoder */
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    /* put sample parameters */
    c->bit_rate = 400000;
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
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(c->priv_data, "preset", "slow", 0);

    /* open it */
    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
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

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame data\n");
        exit(1);
    }

    size = 1.5*320*160;
    img = (unsigned char*)malloc(size*sizeof(unsigned char));
    img_file_path = "/home/dhruva/my_sample/2016-06-08--11-46-01.yuv";
    istream = fopen(img_file_path, "r");

    int u_index = (c->width)*(c->height);
    int v_index = u_index + (c->width)*(c->height)/4;

    /* encode 1 second of video */
    do {// 18177
        fflush(stdout);

        read_size = fread(img, sizeof(unsigned char), size, istream);
        eofile = !read_size;

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
        encode(c, frame, pkt, f);
    } while (i);
    
    fclose(istream);

    /* flush the encoder */
    encode(c, NULL, pkt, f);

    /* Add sequence end code to have a real MPEG file.
       It makes only sense because this tiny examples writes packets
       directly. This is called "elementary stream" and only works for some
       codecs. To create a valid file, you usually need to write packets
       into a proper file format or protocol; see mux.c.
     */
    if (codec->id == AV_CODEC_ID_MPEG1VIDEO || codec->id == AV_CODEC_ID_MPEG2VIDEO)
        fwrite(endcode, 1, sizeof(endcode), f);
    fclose(f);

    // avg = sum/150.0;
    // printf("Avg Time for Encoding: %f \n", avg);

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
