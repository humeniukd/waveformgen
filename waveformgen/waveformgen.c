/*
 waveformgen.c
 
 This file is part of waveformgen.
 
 waveformgen is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 waveformgen is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with waveformgen. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/avfiltergraph.h"
#include "libavfilter/avcodec.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"

#include "waveformgen.h"

#if HAVE_PTHREADS
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#define WFG_STRING_BUFFER_SIZE 7

//#define SHORT_SZ sizeof(short)

// private
char* lastErrorMessage = "No Error so far.";

double max_val = 0;

AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static AVFormatContext *ic, *oc;
static AVCodecContext *c, *co;
static	AVRational time_base;
static	AVStream *st;
static   AVCodec *codec, *ocodec;
static	AVOutputFormat *fmt;
static int audio_stream_index = -1;
static int needs_transcoding = 0;

static int open_input_file(const char *filename)
{
    int ret;
    
    if ((ret = avformat_open_input(&ic, filename, NULL, NULL)) < 0) {
        return ret;
    }
    
    if ((ret = avformat_find_stream_info(ic, NULL)) < 0) {
        return ret;
    }
    
    /* select the audio stream */
    ret = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot find a audio stream in the input file\n");
        return ret;
    }
    audio_stream_index = ret;
    c = ic->streams[audio_stream_index]->codec;
    av_opt_set_int(c, "refcounted_frames", 1, 0);
    
    /* init the audio decoder */
    if ((ret = avcodec_open2(c, codec, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot open audio decoder\n");
        return ret;
    }
    needs_transcoding = 0;//(c->codec_id != AV_CODEC_ID_MP3);
    return 0;
}

/*
 * add an audio output stream
 */
static AVStream *add_audio_stream(AVFormatContext *oc, AVCodec **codec,
                                  enum AVCodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;
    
    
    /* find the audio encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find codec\n");
        exit(1);
    }
    
    st = avformat_new_stream(oc, *codec);
    if (!st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    st->id = 1;
    
    c = st->codec;
    
    /* put sample parameters */
    c->sample_fmt  = AV_SAMPLE_FMT_S16P;
    c->bit_rate    = 128000;
    c->sample_rate = 44100;
    c->channels    = 2;
    c->channel_layout = av_get_default_channel_layout(c->channels);
    
    // some formats want stream headers to be separate
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;
    return st;
}

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret;
    AVFilter *abuffersrc  = avfilter_get_by_name("abuffer");
    AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    const enum AVSampleFormat out_sample_fmts[] = { AV_SAMPLE_FMT_S16P, -1 };
    const int64_t out_channel_layouts[] = { AV_CH_LAYOUT_STEREO, AV_CH_LAYOUT_MONO, -1 };
    const int out_sample_rates[] = { 44100, -1 };
    const AVFilterLink *outlink;
    AVRational time_base = ic->streams[audio_stream_index]->time_base;
    
    filter_graph = avfilter_graph_alloc();
    
    /* buffer audio source: the decoded frames from the decoder will be inserted here. */
    if (!c->channel_layout)
        c->channel_layout = av_get_default_channel_layout(c->channels);
    snprintf(args, sizeof(args),
             "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
             time_base.num, time_base.den, c->sample_rate,
             av_get_sample_fmt_name(c->sample_fmt), c->channel_layout);
    ret = avfilter_graph_create_filter(&buffersrc_ctx, abuffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer source\n");
        return ret;
    }
    
    /* buffer audio sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot create audio buffer sink\n");
        return ret;
    }
    
    ret = av_opt_set_int_list(buffersink_ctx, "sample_fmts", out_sample_fmts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample format\n");
        return ret;
    }
    
    ret = av_opt_set_int_list(buffersink_ctx, "channel_layouts", out_channel_layouts, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output channel layout\n");
        return ret;
    }
    
    ret = av_opt_set_int_list(buffersink_ctx, "sample_rates", out_sample_rates, -1,
                              AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output sample rate\n");
        return ret;
    }
    
    /* Endpoints for the filter graph. */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
    
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
        return ret;
    
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        return ret;
    
    /* Print summary of the sink buffer
     * Note: args buffer is reused to store channel layout string */
    outlink = buffersink_ctx->inputs[0];
    av_get_channel_layout_string(args, sizeof(args), -1, outlink->channel_layout);
    av_log(NULL, AV_LOG_INFO, "Output: srate:%dHz fmt:%s chlayout:%s\n",
           (int)outlink->sample_rate,
           (char *)av_x_if_null(av_get_sample_fmt_name(outlink->format), "?"),
           args);
    
    return 0;
}

bool wfg_generateImage(char* audioFileName, char* mp3FileName, int width)
{
    int size, channels, ret, got_frame, got_packet;
    uint64_t samples;
//    fdata = malloc(sizeof(AVBPrint));
//    av_bprint_init(fdata,-1,65536);
    
    FILE *outfile = NULL;
    uint64_t dts = 0;
    long samplesPerLine = 0,frames=0;
    
    AVPacket pkt = { 0 }, opkt = { 0 };
    AVFrame *frame = av_frame_alloc();
    AVFrame *filt_frame = av_frame_alloc();
    
    if (!frame || !filt_frame) {
        perror("Could not allocate frame");
        exit(1);
    }
    avcodec_register_all();
    avfilter_register_all();
    av_register_all();
    
    open_input_file(audioFileName);
    
    av_init_packet(&pkt);
    
    samples = ic->duration*c->sample_rate/AV_TIME_BASE;
    
    if (needs_transcoding) {
        /* allocate the output media context */
        avformat_alloc_output_context2(&oc, NULL, NULL, mp3FileName);
        if (!oc) {
            printf("Could not deduce output format from file extension: using MP3.\n");
            avformat_alloc_output_context2(&oc, NULL, "mp3", mp3FileName);
        }
        if (!oc) {
            return 1;
        }
        fmt = oc->oformat;
        
        /* Add the audio and video streams using the default format codecs
         * and initialize the codecs. */
        st = NULL;
        
        if (fmt->audio_codec != AV_CODEC_ID_NONE) {
            st = add_audio_stream(oc, &ocodec, fmt->audio_codec);
        }
        
        /* Now that all the parameters are set, we can open the audio and
         * video codecs and allocate the necessary encode buffers. */
        if (st)
        {
            co = st->codec;
            /* open it */
            if (avcodec_open2(co, ocodec, NULL) < 0) {
                fprintf(stderr, "could not open codec\n");
                exit(1);
            }
        }
        av_dump_format(oc, 0, mp3FileName, 1);
        
        if (avio_open(&oc->pb, mp3FileName, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Could not open '%s'\n", mp3FileName);
            return 1;
        }
        
        /* Write the stream header, if any. */
        if (avformat_write_header(oc, NULL) < 0) {
            fprintf(stderr, "Error occurred when opening output file\n");
            return 1;
        }
        
        co = st->codec;
        
        av_buffersink_set_frame_size(buffersink_ctx,co->frame_size);
    }
    //TODO: right linesize calc
    samplesPerLine = samples/1800;
    char filter_descr[16];
    sprintf(filter_descr, "wf=n=%ld", samplesPerLine);
    if ((ret = init_filters(filter_descr)) < 0)
        goto end;
    
    av_init_packet(&pkt);
    
    while (av_read_frame(ic, &pkt) >= 0)
    {
        AVPacket orig_pkt = pkt;
        do {
            av_frame_unref(frame);
            int decoded = pkt.size;
            /* decode audio frame */
            ret = avcodec_decode_audio4(c, frame, &got_frame, &pkt);
            
            if (ret < 0) {
                fprintf(stderr, "Error decoding audio frame\n");
                return ret;
            }
            decoded = FFMIN(ret, pkt.size);
            
            if (got_frame) {
                
                frames += frame->nb_samples;
                if(frame->pts == AV_NOPTS_VALUE) {
                    frame->pts = dts;
                    dts += ((uint64_t)AV_TIME_BASE * frame->nb_samples) /
                    c->sample_rate;
                }
                
                /* push the audio data from decoded frame into the filtergraph */ //AV_BUFFERSRC_FLAG_PUSH
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, 0) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the audio filtergraph\n");
                    break;
                }
                
                /* pull filtered audio from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame_flags(buffersink_ctx, filt_frame, 0); //AV_BUFFERSINK_FLAG_NO_REQUEST
                    //ret = av_buffersink_get_samples(buffersink_ctx, filt_frame,co->frame_size);
                    if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if(ret < 0)
                        goto end;
                    if (needs_transcoding) {
                        
                        av_init_packet(&opkt);
                        ret = avcodec_encode_audio2(co, &opkt, filt_frame, &got_packet);
                        if (ret < 0) {
                            fprintf(stderr, "Error encoding audio frame\n");
                            exit(1);
                        }
                        if (got_packet) {
                            ret = av_write_frame(oc, &opkt);
                            if (ret != 0) {
                                fprintf(stderr, "Error while writing audio frame\n");
                                exit(1);
                            }
                            av_free_packet(&opkt);
                        }
                    }
                    av_frame_unref(filt_frame);
                }
            }
            pkt.data += decoded;
            pkt.size -= decoded;
        } while (pkt.size > 0);
        av_free_packet(&orig_pkt);
    }
    if (needs_transcoding) {
        //Finally write trailer using av_write_trailer.
        av_write_trailer(oc);
        avcodec_close(st->codec);
    }
    
end:
    
    avfilter_graph_free(&filter_graph); //uninit called here
    if(outfile)
        fclose(outfile);
    //    if(wb)
    //	free(wb);
    if(c)
        avcodec_close(c);
    if(ic)
        avformat_close_input(&ic);
    return true;
}

char* wfg_lastErrorMessage()
{
    return lastErrorMessage;
}

int wfg_defaultOptions()
{
    return 1800;
}


