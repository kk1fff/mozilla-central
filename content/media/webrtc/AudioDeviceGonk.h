/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_GONK_H
#define WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_GONK_H

#include "audio_device.h"
#include "audio_device_buffer.h"
#include "ReentrantMonitor.h"
#include "nsAutoPtr.h"
#include "nsIThread.h"

#include "utils/RefBase.h"
#include "media/AudioRecord.h"

namespace mozilla
{
class ReentrantMonitor;
}

namespace webrtc
{
class EventWrapper;

const uint32_t N_REC_SAMPLES_PER_SEC = 44000; // Default is 44.1 kHz
const uint32_t N_REC_CHANNELS = 1; // default is mono recording
const uint32_t N_AUDIO_FORMAT = AUDIO_FORMAT_PCM_16_BIT;
const uint32_t REC_BUF_SIZE_IN_SAMPLES = 480;

class AudioDeviceGeneric;
class AudioDeviceUtility;
class CriticalSectionWrapper;

class AudioDeviceGonk : public AudioDeviceModule
{
public:
  WebRtc_Word32 AttachAudioBuffer();

  AudioDeviceGonk(const WebRtc_Word32 id, const AudioLayer audioLayer);
  virtual ~AudioDeviceGonk();

public: // RefCountedModule
  virtual WebRtc_Word32 ChangeUniqueId(const WebRtc_Word32 id);
  virtual WebRtc_Word32 TimeUntilNextProcess();
  virtual WebRtc_Word32 Process();

public:
  // Factory methods (resource allocation/deallocation)
  static AudioDeviceModule* Create(
    const WebRtc_Word32 id,
    const AudioLayer audioLayer = kPlatformDefaultAudio);

  // Retrieve the currently utilized audio layer
  virtual WebRtc_Word32 ActiveAudioLayer(AudioLayer* audioLayer) const;

  // Error handling
  virtual ErrorCode LastError() const;
  virtual WebRtc_Word32 RegisterEventObserver(
    AudioDeviceObserver* eventCallback);

  // Full-duplex transportation of PCM audio
  virtual WebRtc_Word32 RegisterAudioCallback(
    AudioTransport* audioCallback);

  // Main initializaton and termination
  virtual WebRtc_Word32 Init();
  virtual WebRtc_Word32 Terminate();
  virtual bool Initialized() const;

  // Device enumeration
  virtual WebRtc_Word16 PlayoutDevices();
  virtual WebRtc_Word16 RecordingDevices();
  virtual WebRtc_Word32 PlayoutDeviceName(
    WebRtc_UWord16 index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]);
  virtual WebRtc_Word32 RecordingDeviceName(
    WebRtc_UWord16 index,
    char name[kAdmMaxDeviceNameSize],
    char guid[kAdmMaxGuidSize]);

  // Device selection
  virtual WebRtc_Word32 SetPlayoutDevice(WebRtc_UWord16 index);
  virtual WebRtc_Word32 SetPlayoutDevice(WindowsDeviceType device);
  virtual WebRtc_Word32 SetRecordingDevice(WebRtc_UWord16 index);
  virtual WebRtc_Word32 SetRecordingDevice(WindowsDeviceType device);

  // Audio transport initialization
  virtual WebRtc_Word32 PlayoutIsAvailable(bool* available);
  virtual WebRtc_Word32 InitPlayout();
  virtual bool PlayoutIsInitialized() const;
  virtual WebRtc_Word32 RecordingIsAvailable(bool* available);
  virtual WebRtc_Word32 InitRecording();
  virtual bool RecordingIsInitialized() const;

  // Audio transport control
  virtual WebRtc_Word32 StartPlayout();
  virtual WebRtc_Word32 StopPlayout();
  virtual bool Playing() const;
  virtual WebRtc_Word32 StartRecording();
  virtual WebRtc_Word32 StopRecording();
  virtual bool Recording() const;

  // Microphone Automatic Gain Control (AGC)
  virtual WebRtc_Word32 SetAGC(bool enable);
  virtual bool AGC() const;

  // Volume control based on the Windows Wave API (Windows only)
  virtual WebRtc_Word32 SetWaveOutVolume(WebRtc_UWord16 volumeLeft,
                                         WebRtc_UWord16 volumeRight);
  virtual WebRtc_Word32 WaveOutVolume(WebRtc_UWord16* volumeLeft,
                                      WebRtc_UWord16* volumeRight) const;

  // Audio mixer initialization
  virtual WebRtc_Word32 SpeakerIsAvailable(bool* available);
  virtual WebRtc_Word32 InitSpeaker();
  virtual bool SpeakerIsInitialized() const;
  virtual WebRtc_Word32 MicrophoneIsAvailable(bool* available);
  virtual WebRtc_Word32 InitMicrophone();
  virtual bool MicrophoneIsInitialized() const;

  // Speaker volume controls
  virtual WebRtc_Word32 SpeakerVolumeIsAvailable(bool* available);
  virtual WebRtc_Word32 SetSpeakerVolume(WebRtc_UWord32 volume);
  virtual WebRtc_Word32 SpeakerVolume(WebRtc_UWord32* volume) const;
  virtual WebRtc_Word32 MaxSpeakerVolume(WebRtc_UWord32* maxVolume) const;
  virtual WebRtc_Word32 MinSpeakerVolume(WebRtc_UWord32* minVolume) const;
  virtual WebRtc_Word32 SpeakerVolumeStepSize(
    WebRtc_UWord16* stepSize) const;

  // Microphone volume controls
  virtual WebRtc_Word32 MicrophoneVolumeIsAvailable(bool* available);
  virtual WebRtc_Word32 SetMicrophoneVolume(WebRtc_UWord32 volume);
  virtual WebRtc_Word32 MicrophoneVolume(WebRtc_UWord32* volume) const;
  virtual WebRtc_Word32 MaxMicrophoneVolume(
    WebRtc_UWord32* maxVolume) const;
  virtual WebRtc_Word32 MinMicrophoneVolume(
    WebRtc_UWord32* minVolume) const;
  virtual WebRtc_Word32 MicrophoneVolumeStepSize(
    WebRtc_UWord16* stepSize) const;

  // Speaker mute control
  virtual WebRtc_Word32 SpeakerMuteIsAvailable(bool* available);
  virtual WebRtc_Word32 SetSpeakerMute(bool enable);
  virtual WebRtc_Word32 SpeakerMute(bool* enabled) const;

  // Microphone mute control
  virtual WebRtc_Word32 MicrophoneMuteIsAvailable(bool* available);
  virtual WebRtc_Word32 SetMicrophoneMute(bool enable);
  virtual WebRtc_Word32 MicrophoneMute(bool* enabled) const;

  // Microphone boost control
  virtual WebRtc_Word32 MicrophoneBoostIsAvailable(bool* available);
  virtual WebRtc_Word32 SetMicrophoneBoost(bool enable);
  virtual WebRtc_Word32 MicrophoneBoost(bool* enabled) const;

  // Stereo support
  virtual WebRtc_Word32 StereoPlayoutIsAvailable(bool* available) const;
  virtual WebRtc_Word32 SetStereoPlayout(bool enable);
  virtual WebRtc_Word32 StereoPlayout(bool* enabled) const;
  virtual WebRtc_Word32 StereoRecordingIsAvailable(bool* available) const;
  virtual WebRtc_Word32 SetStereoRecording(bool enable);
  virtual WebRtc_Word32 StereoRecording(bool* enabled) const;
  virtual WebRtc_Word32 SetRecordingChannel(const ChannelType channel);
  virtual WebRtc_Word32 RecordingChannel(ChannelType* channel) const;

  // Delay information and control
  virtual WebRtc_Word32 SetPlayoutBuffer(const BufferType type,
                                         WebRtc_UWord16 sizeMS = 0);
  virtual WebRtc_Word32 PlayoutBuffer(BufferType* type,
                                      WebRtc_UWord16* sizeMS) const;
  virtual WebRtc_Word32 PlayoutDelay(WebRtc_UWord16* delayMS) const;
  virtual WebRtc_Word32 RecordingDelay(WebRtc_UWord16* delayMS) const;

  // CPU load
  virtual WebRtc_Word32 CPULoad(WebRtc_UWord16* load) const;

  // Recording of raw PCM data
  virtual WebRtc_Word32 StartRawOutputFileRecording(
    const char pcmFileNameUTF8[kAdmMaxFileNameSize]);
  virtual WebRtc_Word32 StopRawOutputFileRecording();
  virtual WebRtc_Word32 StartRawInputFileRecording(
    const char pcmFileNameUTF8[kAdmMaxFileNameSize]);
  virtual WebRtc_Word32 StopRawInputFileRecording();

  // Native sample rate controls (samples/sec)
  virtual WebRtc_Word32 SetRecordingSampleRate(
    const WebRtc_UWord32 samplesPerSec);
  virtual WebRtc_Word32 RecordingSampleRate(
    WebRtc_UWord32* samplesPerSec) const;
  virtual WebRtc_Word32 SetPlayoutSampleRate(
    const WebRtc_UWord32 samplesPerSec);
  virtual WebRtc_Word32 PlayoutSampleRate(
    WebRtc_UWord32* samplesPerSec) const;

  // Mobile device specific functions
  virtual WebRtc_Word32 ResetAudioDevice();
  virtual WebRtc_Word32 SetLoudspeakerStatus(bool enable);
  virtual WebRtc_Word32 GetLoudspeakerStatus(bool* enabled) const;

  virtual int32_t EnableBuiltInAEC(bool enable);
  virtual bool BuiltInAECIsEnabled() const;

public:
  WebRtc_Word32 Id() {return mId;}

private:
  int InitRecorderLocked(int audioSource, int sampleRate);
  WebRtc_Word32 InitSampleRate();
  void RecThreadFunc();

  // Lock
  void Lock()
  {
    mCritSect.Enter();
  };

  void UnLock()
  {
    mCritSect.Exit();
  };
private:
  mutable mozilla::ReentrantMonitor mCritSect;
  mozilla::ReentrantMonitor mCritSectEventCb;
  mozilla::ReentrantMonitor mCritSectAudioCb;

  AudioDeviceObserver* mPtrCbAudioDeviceObserver;

  AudioDeviceBuffer mAudioDeviceBuffer;

  WebRtc_Word32 mId;
  WebRtc_UWord32 mLastProcessTime;
  bool mInitialized;
  mutable ErrorCode mLastError;

  // Events
  EventWrapper& mTimeEventRec;
  EventWrapper& mRecStartStopEvent;

  // Threads
  nsRefPtr<nsIThread> mThreadRec;
  bool mRecThreadIsInitialized;
  bool mShutdownRecThread;

  // Rec buffer
  int8_t mRecBuffer[2 * REC_BUF_SIZE_IN_SAMPLES];

  // States
  bool mRecordingDeviceIsSpecified;
  bool mRecording;
  bool mRecIsInitialized;
  bool mMicIsInitialized;

  // Signal flags to threads
  bool mStartRec;
  bool mStopRec;

  // Warnings and errors
  WebRtc_UWord16 mRecWarning;
  WebRtc_UWord16 mRecError;

  // Delay
  WebRtc_UWord16 mDelayRecording;

  // Stored device properties
  WebRtc_UWord16 mSamplingFreqIn; // Sampling frequency for Mic
  // Stores the desired audio source to use, set in SetRecordingDevice
  int mRecAudioSource;

  // porting from Android JAVA layer
  int mBufferedRecSamples;
  int mPlayPosition;
  android::AudioRecord* mAudioRecord;

  android::sp<android::IAudioFlinger> mAudioFlinger;
};

}  // namespace webrtc

#endif  // WEBRTC_AUDIO_DEVICE_AUDIO_DEVICE_GONK_H
