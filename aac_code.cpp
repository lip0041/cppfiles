#include <fdk-aac/aacdecoder_lib.h>
#include <fdk-aac/aacenc_lib.h>
#include <fstream>
#include <iostream>
#include <memory>
extern "C" {
#include <libavformat/avformat.h>
}

using namespace std;
int main()
{
    const char* inputfile  = "luca.aac";
    const char* outputfile = "lucaa.aac";
    auto        outfile    = make_unique<ofstream>();
    outfile->open(outputfile, ios::out | ios::binary);

    AVFormatContext* input_fmt_ctx = nullptr;
    int              ret;

    if ((ret = avformat_open_input(&input_fmt_ctx, inputfile, nullptr, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "cannot open input file\n");
        return ret;
    }

    if ((ret = avformat_find_stream_info(input_fmt_ctx, nullptr)) < 0) {
        av_log(nullptr, AV_LOG_ERROR, "cannot find stream info\n");
        return ret;
    }

    int audio_stream_index = -1;
    for (int i = 0; i < input_fmt_ctx->nb_streams; ++i) {
        AVStream* stream = input_fmt_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index < 0) {
        av_log(nullptr, AV_LOG_WARNING, "cannot find audio stream\n");
        return 0;
    }

    auto              handle   = aacDecoder_Open(TT_MP4_ADTS, 1);
    HANDLE_AACENCODER enhandle = nullptr;
    aacEncOpen(&enhandle, 0, 2);
    aacEncoder_SetParam(enhandle, AACENC_AOT, AOT_AAC_LC);
    aacEncoder_SetParam(enhandle, AACENC_TRANSMUX, TT_MP4_ADTS);
    aacEncoder_SetParam(enhandle, AACENC_CHANNELMODE, 2);
    aacEncoder_SetParam(enhandle, AACENC_CHANNELORDER, 1);
    aacEncoder_SetParam(enhandle, AACENC_GRANULE_LENGTH, 1024);
    aacEncoder_SetParam(enhandle, AACENC_SAMPLERATE, 48000);
    aacEncoder_SetParam(enhandle, AACENC_BITRATE, 48000 / 44 * 128);
    aacEncoder_SetParam(enhandle, AACENC_AFTERBURNER, 1);

    aacEncEncode(enhandle, nullptr, nullptr, nullptr, nullptr);
    uint8_t* buffer = new uint8_t[2048 * 2 * sizeof(INT_PCM)];
    AVPacket packet;
    uint32_t validsize = 0;

    AACENC_InfoStruct eninfo = {0};
    aacEncInfo(enhandle, &eninfo);
    cout << "bufbytes: " << eninfo.maxOutBufBytes << ", framelength: " << eninfo.frameLength << endl;

    uint8_t* out_buf = new uint8_t[2 * 2 * eninfo.maxOutBufBytes];

    while (true) {
        if (ret = av_read_frame(input_fmt_ctx, &packet) < 0) {
            break;
        }

        do {
            UCHAR*   inbuffer = (UCHAR*)packet.data;
            uint32_t insize   = packet.size;
            validsize         = packet.size;

            if (packet.stream_index == audio_stream_index) {
                ret = aacDecoder_Fill(handle, &inbuffer, &insize, &validsize);
                memset(buffer, 0, 2048 * 2 * sizeof(INT_PCM));
                ret = aacDecoder_DecodeFrame(handle, (INT_PCM*)buffer, 2048 * 2, 0);

                if (ret == AAC_DEC_OK) {
                    CStreamInfo* info   = aacDecoder_GetStreamInfo(handle);
                    uint32_t     outlen = info->frameSize * info->numChannels * sizeof(INT_PCM);
                    cout << "channel: " << info->numChannels << ", sample: " << info->sampleRate
                         << ", size: " << info->frameSize << ", :" << info->aot << endl;

                    AACENC_BufDesc inBuf   = {0};
                    AACENC_BufDesc outBuf  = {0};
                    AACENC_InArgs  inArgs  = {0};
                    AACENC_OutArgs outArgs = {0};

                    int32_t inIdentifier    = IN_AUDIO_DATA;
                    int32_t inElemSize      = 2;
                    inArgs.numInSamples     = 1024 * 2;
                    inBuf.numBufs           = 1;
                    inBuf.bufs              = reinterpret_cast<void**>(&buffer);
                    inBuf.bufferIdentifiers = &inIdentifier;
                    inBuf.bufSizes          = (INT*)&outlen;
                    inBuf.bufElSizes        = &inElemSize;

                    int     outIdentifier    = OUT_BITSTREAM_DATA;
                    int32_t outElemSize      = 2;
                    outBuf.numBufs           = 1;
                    outBuf.bufs              = reinterpret_cast<void**>(&out_buf);
                    outBuf.bufferIdentifiers = &outIdentifier;
                    outBuf.bufElSizes        = &outElemSize;
                    int32_t outSize          = 2 * 2 * eninfo.frameLength;
                    outBuf.bufSizes          = reinterpret_cast<INT*>(&outSize);

                    aacEncEncode(enhandle, &inBuf, &outBuf, &inArgs, &outArgs);
                    std::cout << "encoded: sample:" << outArgs.numInSamples << ", bytes:" << outArgs.numOutBytes
                              << endl;
                    outfile->write((char*)out_buf, outArgs.numOutBytes);
                    outfile->flush();
                }
            }
        } while (validsize > 0 && ret != AAC_DEC_NOT_ENOUGH_BITS);
    }

    avformat_close_input(&input_fmt_ctx);
    aacDecoder_Close(handle);
    aacEncClose(&enhandle);
    outfile->close();
    return 0;
}