//
// Created by Morteza on 2/1/2020.
//

#ifndef OPENSLES_PCM_PLAYER_H
#define OPENSLES_PCM_PLAYER_H

#include <memory>
#include <vector>
#include "AudioQueue.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <mutex>

//#define  VOLUME_CONTRROL
class PcmPlayer {
public:
    PcmPlayer();

    ~PcmPlayer() { Release(); }

    void Start();

    SLuint32 GetPlayState();

    bool isPlayingState();

    void Paused();

    void Stop();

    void FeedPcmData(uint8_t *pcm, size_t size);

    void Init(SLuint32 channels,
              SLuint32 sampleRate = SL_SAMPLINGRATE_44_1,
              SLuint32 bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16);

    void Release();

    void SetVolume(float i);

    void SetPlayerState(bool value);

private:
    struct PcmBufferPool;
    struct PcmBufferBlockingQueue;

    SLObjectItf engineObject;
    SLEngineItf engineEngine;
    SLObjectItf outputMixObject;
    SLObjectItf playerObject;
    SLPlayItf playerPlay;
    SLAndroidSimpleBufferQueueItf audioBufferQueue;

    std::shared_ptr<PcmBufferPool> pcmBufferPool;
    std::shared_ptr<PcmBufferBlockingQueue> pcmBufferBlockingQueue;

#if VOLUME_CONTRROL
    SLEffectSendItf playerEffectSend;
    SLVolumeItf playerVolume;
    SLmillibel currentVolume = SL_MILLIBEL_MIN;
    SLmillibel maxVolume = SL_MILLIBEL_MIN;
#endif
    typedef std::vector<uint8_t> PcmBuffer;
    PcmBuffer pcmBuffer;
    bool callBacked;

    std::mutex class_mutex;
    friend void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *pcmPlayer);

    volatile bool isPlaying;
};

#endif //OPENSLES_PCM_PLAYER_H
