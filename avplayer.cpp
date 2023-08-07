#include <SDL2/SDL.h>
#include <chrono>
#include <iostream>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

#define SDL_MAIN_HANDLED

// SDL
SDL_Window*   window;
SDL_Renderer* renderer;
SDL_Texture*  texture;
SDL_Rect      rect;

int            thread_quit  = 0;
int            thread_pause = 0;
unsigned int   audioLen     = 0;
unsigned char* audioChunk   = nullptr;
unsigned char* audioPos     = nullptr;
struct FFmpegData {
    AVFormatContext* inputContext;
    AVCodecContext*  vcodecContext;
    AVCodecContext*  acodecContext;
    int              video_stream_index;
    int              audio_stream_index;
    AVFrame*         frame;
};

int thread_func(void* data)
{
    int      ret;
    int64_t  start_time = av_gettime();
    AVPacket packet;

    AVFormatContext* input_ctx     = ((FFmpegData*)data)->inputContext;
    AVCodecContext*  vcodecContext = ((FFmpegData*)data)->vcodecContext;
    AVCodecContext*  acodecContext = ((FFmpegData*)data)->acodecContext;
    int              video_index   = ((FFmpegData*)data)->video_stream_index;
    int              audio_index   = ((FFmpegData*)data)->audio_stream_index;
    AVFrame*         frame         = ((FFmpegData*)data)->frame;
    bool             isFirst       = true;
    int64_t          sec;

    struct SwrContext* convertContext = swr_alloc();
    AVSampleFormat     in_sample_fmt  = acodecContext->sample_fmt;
    // AVSampleFormat     out_sample_fmt = AV_SAMPLE_FMT_S16;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLT;
    std::cout << "in sample fmt: " << in_sample_fmt << ", out sample fmt: " << out_sample_fmt << std::endl;
    int in_sample_rate  = acodecContext->sample_rate;
    int out_sample_rate = acodecContext->sample_rate;
    std::cout << "in sample rate: " << in_sample_rate << ", out sample rate: " << out_sample_rate << std::endl;
    AVChannelLayout in_ch_layout  = acodecContext->ch_layout;
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    std::cout << "in sample channel: " << in_ch_layout.nb_channels
              << ", out sample channel: " << out_ch_layout.nb_channels << std::endl;

    ret = swr_alloc_set_opts2(&convertContext, &out_ch_layout, out_sample_fmt, out_sample_rate, &in_ch_layout,
                              in_sample_fmt, in_sample_rate, 0, nullptr);

    swr_init(convertContext);

    int out_channel_nb = out_ch_layout.nb_channels;
    // uint8_t* buffer         = (uint8_t*)av_malloc(2 * acodecContext->frame_size * out_channel_nb);
    uint8_t** buffer;
    int       buffer_size;
    ret = av_samples_alloc_array_and_samples(&buffer, &buffer_size, out_ch_layout.nb_channels,
                                             acodecContext->frame_size, out_sample_fmt, 0);
    std::cout << "buffer_size: " << buffer_size << std::endl;

    while (!thread_quit) {
        if (thread_pause) {
            std::cout << "thread pause, delay 10ms\n";
            SDL_Delay(10);
            continue;
        }

        if ((ret = av_read_frame(input_ctx, &packet)) < 0) {
            break;
        }
        // std::cout << "start send one packet\n";
        if (packet.stream_index == audio_index) {
            ret = avcodec_send_packet(acodecContext, &packet);
            while (ret >= 0) {
                ret = avcodec_receive_frame(acodecContext, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(nullptr, AV_LOG_ERROR, "error while sending a packet to the decoder\n");
                    break;
                }
                int out_nb_samples = swr_get_out_samples(convertContext, frame->nb_samples);
                swr_convert(convertContext, buffer, out_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);

                while (audioLen > 0) {
                    SDL_Delay(1);
                }

                audioChunk = (unsigned char*)*buffer;
                audioPos   = audioChunk;
                audioLen = buffer_size;
            }
        } else if (packet.stream_index == video_index) {
            ret = avcodec_send_packet(vcodecContext, &packet);

            // std::cout << "start send one frame\n";
            while (ret >= 0) {
                ret = avcodec_receive_frame(vcodecContext, frame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(nullptr, AV_LOG_ERROR, "error while sending a packet to the decoder\n");
                    break;
                }
                // SDL播放YUV
                SDL_UpdateYUVTexture(texture, &rect, frame->data[0], frame->linesize[0], frame->data[1],
                                     frame->linesize[1], frame->data[2], frame->linesize[2]);
                // std::cout << "y_size: " << frame->linesize[0] << std::endl;
                // std::cout << "u_size: " << frame->linesize[1] << std::endl;
                // std::cout << "v_size: " << frame->linesize[2] << std::endl;
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, &rect);
                SDL_RenderPresent(renderer);
                AVRational time_base_q = {1, AV_TIME_BASE};
                if (isFirst) {
                    // rtmp获取的流不一定是从00:00:00开始的
                    // 根据当前帧的pts获取其已播放的us数
                    sec = frame->pts * av_q2d(input_ctx->streams[video_index]->time_base) * 1000 * 1000;
                    // dur为当前帧的播放时间
                    int64_t dur =
                        av_rescale_q(frame->duration, input_ctx->streams[video_index]->time_base, time_base_q);
                    // std::cout << "dur: " << dur << "\n";
                    sec -= dur;
                    isFirst = false;
                }
                // 控制延时
                int64_t pts_time = av_rescale_q(frame->pts, input_ctx->streams[video_index]->time_base, time_base_q);
                int64_t now_time = av_gettime() - start_time; // us
                // std::cout << "sec: " << sec << ", pts: " << frame->pts << ", duration: " << frame->duration << "\n";
                // std::cout << "pts_time: " << pts_time << ", now_time: " << now_time << "\n";
                // std::cout << "need sleep " << pts_time - now_time - sec << "\n";

                // 比对当前时间与应显示的时间，单位us
                if (pts_time - sec > now_time) {
                    av_usleep(pts_time - now_time - sec);
                }
                av_frame_unref(frame);
            }
            // std::cout << "end send one frame\n";
        }
        av_packet_unref(&packet);
        // std::cout << "end send one packet\n";
    }

    // flush decoder
    packet.data = nullptr;
    packet.size = 0;

    while (1) {
        if (thread_pause) {
            break;
        }
        std::cout << "flush decoder in\n";
        ret = avcodec_send_packet(vcodecContext, &packet); // send empty packet to vcodecContext
        if (ret < 0) {
            break;
        }
        while (ret >= 0) {
            // read last frame in the vcodecContext
            ret = avcodec_receive_frame(vcodecContext, frame);

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "error while sending a packet into decoder\n");
                break;
            }
            SDL_UpdateYUVTexture(texture, &rect, frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1],
                                 frame->data[2], frame->linesize[2]);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, &rect);
            SDL_RenderPresent(renderer);

            AVRational time_base_q = {1, AV_TIME_BASE};
            int64_t    pts_time    = av_rescale_q(frame->pts, input_ctx->streams[video_index]->time_base, time_base_q);
            int64_t    now_time    = av_gettime() - start_time - sec;
            if (pts_time - sec > now_time) {
                av_usleep(pts_time - now_time - sec);
            }

            av_frame_unref(frame);
        }
        std::cout << "flush decoder out\n";
    }

    // notify main thread to exit
    swr_free(&convertContext);
    SDL_Event eve;
    eve.type = SDL_QUIT;
    SDL_PushEvent(&eve);
    return 0;
}

void fill_audio(void* udata, uint8_t* stream, int len)
{
    SDL_memset(stream, 0, len);

    if (audioLen == 0) {
        return;
    }
    len = (len > audioLen ? audioLen : len);
    SDL_MixAudio(stream, audioPos, len, SDL_MIX_MAXVOLUME);
    audioPos += len;
    audioLen -= len;
}

int main()
{
    thread_quit                    = 0;
    thread_pause                   = 0;
    const char*      filename      = "luca_720p_24gop.mp4";
    const char*      url           = "rtmp://127.0.0.1:1935/live/test";
    AVFormatContext* input_fmt_ctx = nullptr;
    int              ret;
    (void)filename;
    (void)url;

    avformat_network_init();
    if ((ret = avformat_open_input(&input_fmt_ctx, filename, nullptr, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(input_fmt_ctx, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "cannot find stream info\n");
        return ret;
    }

    int video_stream_index = -1;
    for (int i = 0; i < input_fmt_ctx->nb_streams; ++i) {
        AVStream* stream = input_fmt_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    int audio_stream_index = -1;
    for (int i = 0; i < input_fmt_ctx->nb_streams; ++i) {
        AVStream* stream = input_fmt_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }
    if (audio_stream_index < 0 && video_stream_index < 0) {
        av_log(nullptr, AV_LOG_ERROR, "no audio && video stream\n");
        avformat_close_input(&input_fmt_ctx);
        return 0;
    }

    if (video_stream_index < 0) {
        av_log(nullptr, AV_LOG_WARNING, "cannot find video stream\n");
    }

    if (audio_stream_index < 0) {
        av_log(nullptr, AV_LOG_WARNING, "cannot find audio stream\n");
    }

    // 解码器
    AVCodecParameters* codecpar_v   = input_fmt_ctx->streams[video_stream_index]->codecpar;
    auto               input_vcodec = avcodec_find_decoder(codecpar_v->codec_id);
    std::cout << "codec_id: " << codecpar_v->codec_id << std::endl;
    if (input_vcodec == nullptr) {
        av_log(nullptr, AV_LOG_ERROR, "could not find decoder for codec id\n");
        avformat_close_input(&input_fmt_ctx);
        return AVERROR(ENOMEM);
    }

    AVCodecContext* vcodecContext = avcodec_alloc_context3(input_vcodec);
    if (!vcodecContext) {
        av_log(nullptr, AV_LOG_ERROR, "could not allocate a decoding context\n");
        avformat_close_input(&input_fmt_ctx);
        return AVERROR(ENOMEM);
    }

    if ((ret = avcodec_parameters_to_context(vcodecContext, codecpar_v)) < 0) {
        avformat_close_input(&input_fmt_ctx);
        avcodec_free_context(&vcodecContext);
        return ret;
    }

    if ((ret = avcodec_open2(vcodecContext, input_vcodec, nullptr)) < 0) {
        avcodec_free_context(&vcodecContext);
        avformat_close_input(&input_fmt_ctx);
        return ret;
    }

    // 解码器
    AVCodecParameters* codecpar_a   = input_fmt_ctx->streams[audio_stream_index]->codecpar;
    auto               input_acodec = avcodec_find_decoder(codecpar_a->codec_id);
    std::cout << "codec_id: " << codecpar_a->codec_id << std::endl;
    if (input_acodec == nullptr) {
        av_log(nullptr, AV_LOG_ERROR, "could not find decoder for codec id\n");
        avformat_close_input(&input_fmt_ctx);
        return AVERROR(ENOMEM);
    }

    AVCodecContext* acodecContext = avcodec_alloc_context3(input_acodec);
    if (!acodecContext) {
        av_log(nullptr, AV_LOG_ERROR, "could not allocate a decoding context\n");
        avformat_close_input(&input_fmt_ctx);
        return AVERROR(ENOMEM);
    }

    if ((ret = avcodec_parameters_to_context(acodecContext, codecpar_a)) < 0) {
        avformat_close_input(&input_fmt_ctx);
        avcodec_free_context(&acodecContext);
        return ret;
    }

    if ((ret = avcodec_open2(acodecContext, input_acodec, nullptr)) < 0) {
        avcodec_free_context(&acodecContext);
        avformat_close_input(&input_fmt_ctx);
        return ret;
    }

    // 解码
    AVFrame* frame = av_frame_alloc();
    int      srcW  = codecpar_v->width;
    int      srcH  = codecpar_v->height;
    int      dstW  = srcW;
    int      dstH  = srcH;

    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("init audio video subsystem failed.");
        return 0;
    }

    int screenW = dstW;
    int screenH = dstH;

    ret = SDL_CreateWindowAndRenderer(screenW, screenH, SDL_WINDOW_RESIZABLE, &window, &renderer);
    if (ret < 0) {
        return 1;
    }

    SDL_SetWindowTitle(window, "player");
    SDL_ShowWindow(window);

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TextureAccess::SDL_TEXTUREACCESS_STREAMING, screenW,
                                screenH);
    if (!texture) {
        return 1;
    }

    SDL_AudioSpec wantSpec;
    // wantSpec.freq     = codecpar_a->sample_rate;
    wantSpec.freq = acodecContext->sample_rate;
    // wantSpec.freq     = 48000;
    wantSpec.format   = AUDIO_F32SYS; // AV_SAMPLE_FMT_FLT corresponding sdl enum
    // wantSpec.format   = AUDIO_S16SYS;
    wantSpec.silence  = 0;
    wantSpec.samples  = codecpar_a->frame_size; // same to nb_samples
    wantSpec.callback = fill_audio;
    wantSpec.userdata = acodecContext;

    if (ret = SDL_OpenAudio(&wantSpec, nullptr) < 0) {
        SDL_Log("can't open SDL");
        std::cout << "errmsg: " << SDL_GetError() << std::endl;
        return -1;
    }

    SDL_PauseAudio(0);

    rect = SDL_Rect{0, 0, screenW, screenH};

    SDL_Event  event;
    FFmpegData data;
    data.inputContext       = input_fmt_ctx;
    data.vcodecContext      = vcodecContext;
    data.acodecContext      = acodecContext;
    data.frame              = frame;
    data.video_stream_index = video_stream_index;
    data.audio_stream_index = audio_stream_index;
    auto        start       = std::chrono::system_clock::now();
    SDL_Thread* sdl_thread  = SDL_CreateThread(thread_func, nullptr, &data);

    while (1) {
        SDL_WaitEvent(&event);
        if (event.type == SDL_QUIT) {
            thread_quit = 1;
            break;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_SPACE) {
                thread_pause = !thread_pause;
                SDL_PauseAudio(thread_pause); // 1 for playing mute
            }
        }
    }
    // it doesn't seem to matter if you want these destroys or not
    // SDL_DestroyTexture(texture);
    // SDL_DestroyRenderer(renderer);
    // SDL_DestroyWindow(window);
    SDL_Quit();

    auto end = std::chrono::system_clock::now();

    std::chrono::duration<double, std::milli> diff = end - start;
    std::cout << "play time: " << diff.count() << "\n";
    avcodec_free_context(&vcodecContext);
    avcodec_free_context(&acodecContext);
    avformat_close_input(&input_fmt_ctx);
    av_frame_free(&frame);
}
