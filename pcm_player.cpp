//
// Created by Morteza on 2/1/2020.
//

#include <climits>
#include <assert.h>
#include "log.h"
#include <thread>


#include "pcm_player.h"

#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <cassert>
#include <math.h>
#include <list>
#include <unistd.h>

#define SLEEP(x) /* Client system sleep function to sleep x milliseconds
would replace SLEEP macro */

struct PcmPlayer::PcmBufferBlockingQueue {
    void Enqueue(const PcmBuffer &pcmBuffer, size_t maxSize) {
        std::unique_lock<std::mutex> lock(queue_mutex);

        queue_cond.wait(lock, [this, maxSize] { return queue.size() <= maxSize; });

        queue.push_back(pcmBuffer);
        queue_cond.notify_one();
    }

    PcmBuffer Dequeue() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        queue_cond.wait(lock, [this] { return !queue.empty(); });
        auto buffer = queue.front();
        queue.pop_front();
        queue_cond.notify_one();
        return buffer;
    }

    unsigned long queueSize() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        auto size = queue.size();
        return size;
    }

private:
    std::mutex queue_mutex;
    std::condition_variable queue_cond;
    std::list<PcmBuffer> queue;
};


struct PcmPlayer::PcmBufferPool {
    PcmBuffer Get(size_t size) {
        if (pool.empty()) {
            return PcmBuffer(size);
        } else {
            auto buffer = pool.front();
            pool.pop_front();
            return buffer;
        }
    }

    void Return(const PcmBuffer &buffer) {
        pool.push_back(buffer);
    }

private:
    std::list<PcmBuffer> pool;
};


PcmPlayer::PcmPlayer() : engineObject(nullptr),
                         engineEngine(nullptr),
                         outputMixObject(nullptr),
                         playerObject(nullptr),
                         playerPlay(nullptr),
                         audioBufferQueue(nullptr),
                         callBacked(false) {
}

/* Local storage for Audio data in 16 bit words */
#define AUDIO_DATA_STORAGE_SIZE 4096 * 100
/* Audio data buffer size in 16 bit words. 8 data segments are used in
this simple example */
#define AUDIO_DATA_BUFFER_SIZE 1921 //1920
//4096/8

void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context) {
    auto *pcmPlayer = (PcmPlayer *) context;

    /* static auto counter(0);
     if (counter++ == 25) {*/
    //    (*bufferQueue)->Clear(bufferQueue);
//        counter = 0;
//        usleep(100);
    /*}*/


    PcmPlayer::PcmBuffer buffer = pcmPlayer->pcmBufferBlockingQueue->Dequeue();

    auto data_len = buffer.size();
    if (pcmPlayer->pcmBuffer.capacity() < data_len) {
        pcmPlayer->pcmBuffer.resize(data_len);
    }
    memcpy(pcmPlayer->pcmBuffer.data(), buffer.data(), data_len);

    (*bufferQueue)->Enqueue(bufferQueue, pcmPlayer->pcmBuffer.data(),data_len);
    pcmPlayer->pcmBufferPool->Return(buffer);

}

#if VOLUME_CONTRROL
void PlayerEventCallBack(
        SLPlayItf caller,
        void *pContext,
        SLuint32 event
) {
//    auto *pcmPlayer = (PcmPlayer *) pContext;
//
//    switch (event) {
//        case SL_PLAYSTATE_PLAYING:
//            pcmPlayer->SetPlayerState(true);
//            break;
//        case SL_PLAYSTATE_PAUSED:
//            pcmPlayer->SetPlayerState(false);
//            break;
//        case SL_PLAYSTATE_STOPPED:
//            pcmPlayer->SetPlayerState(false);
//            break;
//    }

}
#endif


void PcmPlayer::Init(SLuint32 channels,
                     SLuint32 sampleRate,
                     SLuint32 bitsPerSample) {
    SLresult result;
    // init engine
    result = slCreateEngine(&engineObject, 0, nullptr, 0, nullptr, nullptr);
    assert(result == SL_RESULT_SUCCESS);

    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_TRUE);
    assert(result == SL_RESULT_SUCCESS);


    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(result == SL_RESULT_SUCCESS);

    //init output mix
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, nullptr,
                                              nullptr);
    assert(result == SL_RESULT_SUCCESS);
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(result == SL_RESULT_SUCCESS);

    //init player
    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataLocator_AndroidSimpleBufferQueue dataSourceQueue = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            15};
    SLDataFormat_PCM dataSourceFormat = {
            SL_DATAFORMAT_PCM,
            channels,
            sampleRate,
            bitsPerSample,
            bitsPerSample,
            channels == 1 ? SL_SPEAKER_FRONT_CENTER
                          : SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN
    };
    SLDataSource dataSource = {&dataSourceQueue, &dataSourceFormat};
    SLDataSink dataSink = {&outputMix, nullptr};

#if VOLUME_CONTRROL
    const SLInterfaceID ids[] = {/*SL_IID_BUFFERQUEUE*/SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                                       SL_IID_ANDROIDCONFIGURATION,
                                                       SL_IID_EFFECTSEND,
                                                       SL_IID_VOLUME};
        const SLboolean reqs[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &dataSource, &dataSink,4, ids, reqs);
#else
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE,
                                 SL_IID_ANDROIDCONFIGURATION};
    const SLboolean reqs[] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &playerObject, &dataSource, &dataSink,
                                                2, ids, reqs);
#endif
    assert(result == SL_RESULT_SUCCESS);

    // Obtain the Android configuration interface using a previously configured SLObjectItf.
    SLAndroidConfigurationItf playerConfig = nullptr;
    (*playerObject)->GetInterface(playerObject, SL_IID_ANDROIDCONFIGURATION, &playerConfig);

    // Set the performance mode.
    SLuint32 performanceMode = SL_ANDROID_PERFORMANCE_NONE;
    result = (*playerConfig)->SetConfiguration(playerConfig, SL_ANDROID_KEY_PERFORMANCE_MODE,
                                               &performanceMode, sizeof(performanceMode));
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Leaving AndroidConfiguration::SetConfiguration (SL_RESULT_PRECONDITIONS_VIOLATED) in playerConfig latency");
    }
    SLint32 streamType = SL_ANDROID_STREAM_MEDIA;
    result = (*playerConfig)->SetConfiguration(playerConfig, SL_ANDROID_KEY_STREAM_TYPE,
                                               &streamType, sizeof(SLint32));
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Leaving AndroidConfiguration::SetConfiguration (SL_RESULT_PRECONDITIONS_VIOLATED) in playerConfig stream Type");
    }

    result = (*playerObject)->Realize(playerObject, SL_BOOLEAN_FALSE);
    assert(result == SL_RESULT_SUCCESS);

    result = (*playerObject)->GetInterface(playerObject, SL_IID_PLAY, &playerPlay);
    assert(result == SL_RESULT_SUCCESS);


#if VOLUME_CONTRROL
    result = (*playerPlay)->RegisterCallback(playerPlay, PlayerEventCallBack, this);
    assert(result == SL_RESULT_SUCCESS);

    result = (*playerObject)->GetInterface(playerObject, SL_IID_VOLUME, &playerVolume);
    assert(result == SL_RESULT_SUCCESS);

    result = (*playerObject)->GetInterface(playerObject, SL_IID_EFFECTSEND,&playerEffectSend);
    if (SL_RESULT_SUCCESS != result) {
        LOGE("OPEN SL_ES SL_IID_EFFECTSEND ");
    }

    result = (*playerVolume)->GetMaxVolumeLevel(playerVolume, &maxVolume);
    if (result == SL_RESULT_SUCCESS) {
        LOGE("OPEN SL_ES MAX VOLUME LEVEL IS %d ", maxVolume);
    }

    result = (*playerVolume)->GetVolumeLevel(playerVolume, &currentVolume);
    if (result == SL_RESULT_SUCCESS) {
        LOGE("OPEN SL_ES CURRENT VOLUME LEVEL IS %d ", currentVolume);
    }
#endif

    result = (*playerObject)->GetInterface(playerObject, SL_IID_BUFFERQUEUE, &audioBufferQueue);
    assert(result == SL_RESULT_SUCCESS);
    /*result = (*audioBufferQueue)->RegisterCallback(audioBufferQueue, BufferQueueCallback, this);
    assert(result == SL_RESULT_SUCCESS);*/

    /* pcmBufferPool = std::shared_ptr<PcmBufferPool>(new PcmBufferPool);
     pcmBufferBlockingQueue = std::shared_ptr<PcmBufferBlockingQueue>(new PcmBufferBlockingQueue);*/
    Start();
}

void PcmPlayer::Start() {
    if (playerPlay != nullptr) {
        (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PLAYING);

    }
}

void PcmPlayer::Stop() {
    if (playerPlay != nullptr) {
        (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_STOPPED);
        callBacked = false;
    }
}

void PcmPlayer::Paused() {
    if (playerPlay != nullptr) {
        (*playerPlay)->SetPlayState(playerPlay, SL_PLAYSTATE_PAUSED);
        callBacked = false;
    }
}

SLuint32 PcmPlayer::GetPlayState() {
    SLuint32 playState(0);

    if (playerPlay != nullptr) {
        (*playerPlay)->GetPlayState(playerPlay, &playState);
    }
    return playState;
}

bool PcmPlayer::isPlayingState() {
    return isPlaying;
}

void PcmPlayer::FeedPcmData(uint8_t *pcm, size_t size) {

    /*PcmBuffer buffer = pcmBufferPool->Get(size);
    buffer.resize(size);
    memcpy(buffer.data(), pcm, size);
    pcmBufferBlockingQueue->Enqueue(buffer, 15);*/

    /*if (!callBacked) {
        SLuint32 playState;
        (*playerPlay)->GetPlayState(playerPlay, &playState);
        if (playState != SL_PLAYSTATE_PLAYING) {
            Start();
        }
        (*audioBufferQueue)->Clear(audioBufferQueue);
        //BufferQueueCallback(audioBufferQueue, this);
        callBacked = true;
    } else {*/
    (*audioBufferQueue)->Enqueue(audioBufferQueue, pcm, size);
//    }

    usleep(200);
}

void PcmPlayer::Release() {
    if (playerObject != nullptr) {
        (*playerObject)->Destroy(playerObject);
        playerObject = nullptr;
        playerPlay = nullptr;
        audioBufferQueue = nullptr;
    }

    if (outputMixObject != nullptr) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = nullptr;
    }
    if (engineObject != nullptr) {
        (*engineObject)->Destroy(engineObject);
        engineObject = nullptr;
        engineEngine = nullptr;
    }

    pcmBufferBlockingQueue = nullptr;
    pcmBufferPool = nullptr;
    callBacked = false;
    isPlaying = false;
}

void PcmPlayer::SetVolume(float i) {
#if VOLUME_CONTRROL
    SLVolumeItf volumeItf = playerVolume;
    if (nullptr != volumeItf) {
        (*volumeItf)->SetVolumeLevel(volumeItf, static_cast<SLmillibel>(i * 100)/*1dB = 100mB*/);
        LOGE("OPEN SL_ES CURRENT VOLUME LEVEL IS %d ",
             static_cast<SLmillibel>(i * 100)/*1dB = 100mB*/);
    }
#endif
}

void PcmPlayer::SetPlayerState(bool value) {
    std::unique_lock<std::mutex> lock(class_mutex);
    isPlaying = value;
}

