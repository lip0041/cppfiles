# cppfiles

1. avplayer: ffmpeg + sdl2 实现mp4的音视频播放（暂无音视频同步处理）
2. regex: md5使用以及regex库的使用（解析rtsp）
3. ringbuffer: 一读一写环形队列
4. spinlock: c++使用atomic实现自旋锁(非mutex)
5. taskpool: 事件中心的任务池
6. threadpool: 线程池c++11实现（用future了就不是异步啦）
7. aac_code: 使用fdk-aac对aac文件进行解码为pcm再编码成aac（暂不清楚aac解码成pcm后的通道数和fmt是否是原aac的格式或是其他的什么格式）
8. eventfd: 针对signalfd, eventfd, timerfd进行简要说明，针对eventfd, timerfd进行简单使用
9. split_mp4: ffmpeg拆分MP4，分成h264，和pcm（重采样）
