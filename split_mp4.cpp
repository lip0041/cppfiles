//  g++ split_mp4.cpp -o split_mp4 -lavformat -lavcodec -lavutil -lswresample
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswresample/swresample.h"
}
using namespace std;
int alloc_and_copy(AVPacket *outPacket, const uint8_t *spspps, uint32_t spsppsSize, const uint8_t *in,
                   uint32_t size)
{
    int err;
    int startCodeLen = 3;

    err = av_grow_packet(outPacket, spsppsSize + size + startCodeLen);
    if (err < 0)
    {
        return err;
    }
    if (spspps)
    {
        memcpy(outPacket->data, spspps, spsppsSize);
    }

    (outPacket->data + spsppsSize)[0] = 0;
    (outPacket->data + spsppsSize)[1] = 0;
    (outPacket->data + spsppsSize)[2] = 1;
    memcpy(outPacket->data + spsppsSize + startCodeLen, in, size);
    return 0;
}

int h264_extradata_to_annexb(const unsigned char *extraData, const int extraDataSize, AVPacket *outPacket,
                             int padding)
{
    const unsigned char *pExtraData = nullptr;
    int len = 0;
    int spsUnitNum, ppsUnitNum;
    int unitSize, totalSize = 0;
    unsigned char startCode[] = {0, 0, 0, 1};
    unsigned char *pOut = nullptr;
    int err;

    pExtraData = extraData + 4;
    len = (*pExtraData++ & 0x3) + 1;

    // sps
    spsUnitNum = (*pExtraData++ & 0x1f);
    while (spsUnitNum--)
    {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]);
        pExtraData += 2;
        totalSize += unitSize + sizeof(startCode);

        if (totalSize > INT_MAX - padding)
        {
            av_free(pOut);
            return AVERROR(EINVAL);
        }
        if (pExtraData + unitSize > extraData + extraDataSize)
        {
            av_free(pOut);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&pOut, totalSize + padding)) < 0)
        {
            return err;
        }
        memcpy(pOut + totalSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        memcpy(pOut + totalSize - unitSize, pExtraData, unitSize);

        pExtraData += unitSize;
    }

    // pps
    ppsUnitNum = (*pExtraData++ & 0x1f);
    while (ppsUnitNum--)
    {
        unitSize = (pExtraData[0] << 8 | pExtraData[1]);
        pExtraData += 2;
        totalSize += unitSize + sizeof(startCode);

        if (totalSize > INT_MAX - padding)
        {
            av_free(pOut);
            return AVERROR(EINVAL);
        }
        if (pExtraData + unitSize > extraData + extraDataSize)
        {
            av_free(pOut);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&pOut, totalSize + padding)) < 0)
        {
            return err;
        }
        memcpy(pOut + totalSize - unitSize - sizeof(startCode), startCode, sizeof(startCode));
        memcpy(pOut + totalSize - unitSize, pExtraData, unitSize);

        pExtraData += unitSize;
    }
    outPacket->data = pOut;
    outPacket->size = totalSize;

    return len;
}

void DecodeFile(const std::string &inputFile, const std::string &outputFile)
{
    auto outputAudio = outputFile + ".pcm";
    auto outputVideo = outputFile + ".h264";
    AVFormatContext *inputContext = avformat_alloc_context();
    FILE *fpa = fopen(outputAudio.c_str(), "wb");
    FILE *fpv = fopen(outputVideo.c_str(), "wb");
    AVPacket packet;
    const AVCodec *codec = nullptr;
    AVCodecContext *codecContext = nullptr;
    struct SwrContext *convertContext = nullptr;
    AVSampleFormat in_sample_fmt;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int in_sample_rate;
    int out_sample_rate = 48000;
    uint64_t in_channel_layout;
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
    int out_channel_nb = 0;
    uint8_t *buffer = nullptr;
    int video_stream = -1;
    int audio_stream = -1;
    int ret = 0;
    if ((ret = avformat_open_input(&inputContext, inputFile.c_str(), nullptr, nullptr)) != 0)
    {
        // auto str = av_err2str(ret);
        std::cout << "ffmpeg open file failed, " << std::endl;
        return;
    }
    ret = avformat_find_stream_info(inputContext, nullptr);

    video_stream = av_find_best_stream(inputContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream = av_find_best_stream(inputContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video_stream < 0 && audio_stream < 0)
    {
        avformat_close_input(&inputContext);
        return;
    }
    if (audio_stream >= 0)
    {
        av_dump_format(inputContext, audio_stream, inputFile.c_str(), 0);
        codec = avcodec_find_decoder(inputContext->streams[audio_stream]->codecpar->codec_id);
        codecContext = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codecContext, inputContext->streams[audio_stream]->codecpar);
        if (avcodec_open2(codecContext, codec, nullptr) < 0)
        {
            avformat_close_input(&inputContext);
            return;
        }
        convertContext = swr_alloc();
        in_sample_fmt = codecContext->sample_fmt;
        in_sample_rate = codecContext->sample_rate;
        in_channel_layout = codecContext->channel_layout;
        convertContext = swr_alloc_set_opts(convertContext, out_channel_layout, out_sample_fmt, out_sample_rate,
                                            in_channel_layout, in_sample_fmt, in_sample_rate, 0, nullptr);
        swr_init(convertContext);
        out_channel_nb = av_get_channel_layout_nb_channels(out_channel_layout);
        buffer = (uint8_t *)av_malloc(2 * 48000);
    }
    if (video_stream >= 0)
    {
        av_dump_format(inputContext, video_stream, inputFile.c_str(), 0);
    }
    double vfps = 0.0;
    if (inputContext->streams[video_stream]->avg_frame_rate.den == 0 ||
        (inputContext->streams[video_stream]->avg_frame_rate.num == 0 &&
         inputContext->streams[video_stream]->avg_frame_rate.den == 1))
    {
        vfps = inputContext->streams[video_stream]->r_frame_rate.num /
               inputContext->streams[video_stream]->r_frame_rate.den;
    }
    else
    {
        vfps = inputContext->streams[video_stream]->avg_frame_rate.num /
               inputContext->streams[video_stream]->avg_frame_rate.den;
    }
    double afps = (48000 * 2 * 16 / 8) / (1536 * 2 * 16 / 8.0); // aac
    std::cout << "vfps:" << vfps << ", afps: " << afps << std::endl;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;
    bool reading = true;
    int32_t keyCount = 0;
    int32_t writeAcount = 0;
    int32_t writeVcount = 0;
    while (reading)
    {
        static auto timeStart = std::chrono::system_clock::now();
        int error = av_read_frame(inputContext, &packet);
        if (error == AVERROR_EOF)
        {
            // auto stream = inputContext->streams[video_stream];
            // avio_seek(inputContext->pb, 0, SEEK_SET); // io context set to 0
            // avformat_seek_file(inputContext, video_stream, 0, 0, stream->duration, 0);
            // continue;
            reading = false;
            break;
        }
        else if (error < 0)
        {
            break;
        }
        if (audio_stream == packet.stream_index)
        {
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_send_packet(codecContext, &packet);
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(codecContext, frame);

                if (ret < 0)
                {
                    break;
                }
                swr_convert(convertContext, &buffer, 2 * 48000, (const uint8_t **)frame->data,
                            frame->nb_samples);
                int totalSize =
                    av_samples_get_buffer_size(nullptr, out_channel_nb, frame->nb_samples, out_sample_fmt,
                                               0);
                (void)totalSize;
                // std::cout << "totalSize:" << totalSize << std::endl;
                fwrite(buffer, 1, totalSize, fpa);
                // auto inputData = dispatcher_->RequestDataBuffer(MEDIA_TYPE_AUDIO, totalSize);
                // inputData->buff->ReplaceData((const char *)(buffer), totalSize);
                // inputData->isKey = false;
                // inputData->mediaType = MEDIA_TYPE_AUDIO;
                // inputData->pts = packet.pts;
                // // ++ts_;
                // dispatcher_->InputData(inputData);
                ++writeAcount;
                av_frame_unref(frame);
            }
            // auto timeStamp =
            //     timeStart + std::chrono::microseconds(1000000 * 1000 / int(afps * 1000) * writeAcount);
            // std::this_thread::sleep_until(timeStamp);
        }
        else if (video_stream == packet.stream_index)
        {
            unsigned char *pData = packet.data; // frame data
            unsigned char *pEnd = nullptr;
            int dataSize = packet.size;
            int curSize = 0;
            unsigned char nalHeader, nalType;
            AVPacket *outPacket = av_packet_alloc();
            pEnd = pData + dataSize;
            while (curSize < dataSize)
            {
                AVPacket spsppsPacket;
                spsppsPacket.data = nullptr;
                spsppsPacket.size = 0;
                int naluSize = 0;
                if (pEnd - pData < 4)
                {
                    break;
                }
                for (int i = 0; i < 4; ++i)
                {
                    naluSize <<= 8;
                    naluSize |= pData[i];
                }
                pData += 4;
                curSize += 4;
                if (naluSize > (pEnd - pData + 1) || naluSize <= 0)
                {
                    break;
                }
                nalHeader = *pData;
                nalType = nalHeader & 0x1F;
                if (nalType == 6)
                {
                    pData += naluSize;
                    curSize += naluSize;
                    continue;
                }
                else if (nalType == 5)
                {
                    // key frame
                    h264_extradata_to_annexb(
                        inputContext->streams[packet.stream_index]->codecpar->extradata,
                        inputContext->streams[packet.stream_index]->codecpar->extradata_size, &spsppsPacket,
                        AV_INPUT_BUFFER_PADDING_SIZE);
                    if (alloc_and_copy(outPacket, spsppsPacket.data, spsppsPacket.size, pData, naluSize) < 0)
                    {
                        av_packet_free(&outPacket);
                        if (spsppsPacket.data)
                        {
                            free(spsppsPacket.data);
                            spsppsPacket.data = nullptr;
                        }
                        break;
                    }
                }
                else
                {
                    if (alloc_and_copy(outPacket, nullptr, 0, pData, naluSize) < 0)
                    {
                        av_packet_free(&outPacket);
                        if (spsppsPacket.data)
                        {
                            free(spsppsPacket.data);
                            spsppsPacket.data = nullptr;
                        }
                        break;
                    }
                }
                if (spsppsPacket.data)
                {
                    free(spsppsPacket.data);
                    spsppsPacket.data = nullptr;
                }
                curSize += naluSize;
            }
            ++writeVcount;
            fwrite(outPacket->data, 1, outPacket->size, fpv);
            av_packet_unref(outPacket);
            // auto timeStamp =
            //     timeStart + std::chrono::microseconds(1000000 * 1000 / int(vfps * 1000) * writeVcount);
            // std::this_thread::sleep_until(timeStamp);
        }

        av_packet_unref(&packet);
    }
    if (buffer != nullptr)
    {
        free(buffer);
    }
    if (codecContext != nullptr)
    {
        avcodec_close(codecContext);
    }
    avformat_close_input(&inputContext);
    fclose(fpa);
    fclose(fpv);
}

int main(int argc, char** argv)
{
    std::string inputFile = "/home/spoi/cppfiles/";
    std::string outputFile = "out";
    if (argc == 2) {
        inputFile += std::string(argv[1]);
    } else {
        inputFile += "luca_720p.mp4";
    }
    DecodeFile(inputFile, outputFile);
}
