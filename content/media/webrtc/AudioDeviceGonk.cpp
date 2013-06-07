/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "AudioDeviceGonk.h"
#include "audio_device_config.h"
#include "common_audio/signal_processing/include/signal_processing_library.h"
#include "system_wrappers/interface/ref_count.h"
#include "system_wrappers/interface/event_wrapper.h"
#include "mtransport/runnable_utils.h"

#include "nsThreadUtils.h"

#include <assert.h>
#include <string.h>

#include "trace.h"

#define CHECK_INITIALIZED()         \
{                                   \
  if (!mInitialized) {              \
    return -1;                      \
  };                                \
}

#define CHECK_INITIALIZED_BOOL()    \
{                                   \
  if (!mInitialized) {              \
    return false;                   \
  };                                \
}

using namespace mozilla;
using namespace android;

namespace webrtc
{

// ============================================================================
//                                   Static methods
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceModule::Create()
// ----------------------------------------------------------------------------

AudioDeviceModule* AudioDeviceGonk::Create(const WebRtc_Word32 id,
                                                 const AudioLayer audioLayer)
{
  // Create the generic ref counted (platform independent) implementation.
  RefCountImpl<AudioDeviceGonk>* audioDevice =
    new RefCountImpl<AudioDeviceGonk>(id, audioLayer);

  // Ensure that the generic audio buffer can communicate with the
  // platform-specific parts.
  if (audioDevice->AttachAudioBuffer() == -1)
  {
    delete audioDevice;
    return NULL;
  }

  WebRtcSpl_Init();

  return audioDevice;
}

// ============================================================================
//                            Construction & Destruction
// ============================================================================

// ----------------------------------------------------------------------------
//  AudioDeviceGonk - ctor
// ----------------------------------------------------------------------------

AudioDeviceGonk::AudioDeviceGonk(const WebRtc_Word32 id, const AudioLayer audioLayer)
  : mCritSect("ModuleLock")
  , mCritSectEventCb("EventCallbackLock")
  , mCritSectAudioCb("AudioCallbackLock")
  , mPtrCbAudioDeviceObserver(NULL)
  , mId(id)
  , mLastProcessTime(PR_Now() / 1000)
  , mInitialized(false)
  , mLastError(kAdmErrNone)
  , mTimeEventRec(*EventWrapper::Create())
  , mRecStartStopEvent(*EventWrapper::Create())
  , mThreadRec(NULL)
  , mRecThreadIsInitialized(false)
  , mShutdownRecThread(false)
  , mRecordingDeviceIsSpecified(false)
  , mRecording(false)
  , mRecIsInitialized(false)
  , mMicIsInitialized(false)
  , mStartRec(false)
  , mStopRec(false)
  , mRecWarning(0)
  , mRecError(0)
  , mDelayRecording(0)
  , mSamplingFreqIn(N_REC_SAMPLES_PER_SEC/1000)
  , mRecAudioSource(1) // 1 is AudioSource.MIC which is our default
  , mBufferedRecSamples(0)
  , mAudioRecord(NULL)
  , mAudioFlinger(NULL)
{
  WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, id, "%s created", __FUNCTION__);
}

// ----------------------------------------------------------------------------
//  AttachAudioBuffer
//
//  Install "bridge" between the platform implemetation and the generic
//  implementation. The "child" shall set the native sampling rate and the
//  number of channels in this function call.
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::AttachAudioBuffer()
{
  ReentrantMonitorAutoEnter lock(mCritSect);
  WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, mId, "%s", __FUNCTION__);

  mAudioDeviceBuffer.SetId(mId);

  // inform the AudioBuffer about default settings for this implementation
  mAudioDeviceBuffer.SetRecordingSampleRate(N_REC_SAMPLES_PER_SEC);
  mAudioDeviceBuffer.SetRecordingChannels(N_REC_CHANNELS);

  return 0;
}

// ----------------------------------------------------------------------------
//  ~AudioDeviceGonk - dtor
// ----------------------------------------------------------------------------

AudioDeviceGonk::~AudioDeviceGonk()
{
  WEBRTC_TRACE(kTraceMemory, kTraceAudioDevice, mId, "%s destroyed", __FUNCTION__);
  Terminate();
}

// ============================================================================
//                                  Module
// ============================================================================

// ----------------------------------------------------------------------------
//  Module::ChangeUniqueId
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::ChangeUniqueId(const WebRtc_Word32 id)
{
  mId = id;
  return 0;
}

// ----------------------------------------------------------------------------
//  Module::TimeUntilNextProcess
//
//  Returns the number of milliseconds until the module want a worker thread
//  to call Process().
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::TimeUntilNextProcess()
{
  WebRtc_UWord32 now = PR_Now() / 1000;
  WebRtc_Word32 deltaProcess = kAdmMaxIdleTimeProcess - (now - mLastProcessTime);
  return (deltaProcess);
}

// ----------------------------------------------------------------------------
//  Module::Process
//
//  Check for posted error and warning reports. Generate callbacks if
//  new reports exists.
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::Process()
{
  ReentrantMonitorAutoEnter lock(mCritSect);
  mLastProcessTime = PR_Now() / 1000;

  // kRecordingWarning
  if (mRecWarning > 0)
  {
    ReentrantMonitorAutoEnter lock(mCritSectEventCb);
    if (mPtrCbAudioDeviceObserver)
    {
      WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId, "=> OnWarningIsReported(kRecordingWarning)");
      mPtrCbAudioDeviceObserver->OnWarningIsReported(AudioDeviceObserver::kRecordingWarning);
    }
    mRecWarning = 0;
  }

  // kRecordingError
  if (mRecError > 0)
  {
    ReentrantMonitorAutoEnter lock(mCritSectEventCb);
    if (mPtrCbAudioDeviceObserver)
    {
      WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId, "=> OnErrorIsReported(kRecordingError)");
      mPtrCbAudioDeviceObserver->OnErrorIsReported(AudioDeviceObserver::kRecordingError);
    }
    mRecError = 0;
  }

  return 0;
}

// ============================================================================
//                                    Public API
// ============================================================================

// ----------------------------------------------------------------------------
//  ActiveAudioLayer
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::ActiveAudioLayer(AudioLayer* audioLayer) const
{
  *audioLayer = kPlatformDefaultAudio;
  return 0;
}

// ----------------------------------------------------------------------------
//  LastError
// ----------------------------------------------------------------------------

AudioDeviceModule::ErrorCode AudioDeviceGonk::LastError() const
{
  return mLastError;
}

// ----------------------------------------------------------------------------
//  Init
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::Init()
{
  ReentrantMonitorAutoEnter lock(mCritSect);
  if (mInitialized)
      return 0;

  mRecWarning = 0;
  mRecError = 0;
  mAudioFlinger = AudioSystem::get_audio_flinger();

  // Check the sample rate to be used for playback and recording
  // and the max playout volume
  if (InitSampleRate() != 0)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "%s: Failed to init samplerate", __FUNCTION__);
    return -1;
  }

  // RECORDING
  const char threadName[] = "AudioCapture";
  NS_NewNamedThread(threadName, getter_AddRefs(mThreadRec));
  if (mThreadRec == nullptr)
  {
    WEBRTC_TRACE(kTraceCritical, kTraceAudioDevice, mId,
                 "  failed to create the rec audio thread");
    return -1;
  }
  mThreadRec->Dispatch(WrapRunnable(this, &AudioDeviceGonk::RecThreadFunc), NS_DISPATCH_NORMAL);

  mInitialized = true;
  return 0;
}

// ----------------------------------------------------------------------------
//  Terminate
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::Terminate()
{
  ReentrantMonitorAutoEnter lock(mCritSect);

  if (mInitialized)
    return 0;

  // RECORDING
  StopRecording();
  mShutdownRecThread = true;
  mTimeEventRec.Set(); // Release rec thread from waiting state
  if (mThreadRec != nullptr)
  {
    UnLock();
    if (kEventSignaled != mRecStartStopEvent.Wait(5000))
    {
      WEBRTC_TRACE(kTraceError,
                   kTraceAudioDevice,
                   mId,
                   "%s: Recording thread shutdown timed out, cannot "
                   "terminate thread",
                   __FUNCTION__);
      // If we close thread anyway, the app will crash
      Lock();
      return -1;
    }
    mRecStartStopEvent.Reset();

    // Close down rec thread
    mThreadRec->Shutdown();
    Lock();
    mThreadRec = nullptr;
    // Release again, we might have returned to waiting state
    mTimeEventRec.Set();
    mRecThreadIsInitialized = false;
  }
  mMicIsInitialized = false;
  mRecordingDeviceIsSpecified = false;
  mInitialized = false;
  return 0;
}

// ----------------------------------------------------------------------------
//  Initialized
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::Initialized() const
{
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: %d", mInitialized);
  return mInitialized;
}

// ----------------------------------------------------------------------------
//  SpeakerIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerIsAvailable(bool* available)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *available = false;
  return -1;
}

// ----------------------------------------------------------------------------
//  InitSpeaker
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::InitSpeaker()
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneIsAvailable(bool* available)
{
  CHECK_INITIALIZED();

  // We always assume it's available
  *available = true;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return 0;
}

// ----------------------------------------------------------------------------
//  InitMicrophone
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::InitMicrophone()
{
  CHECK_INITIALIZED();
  ReentrantMonitorAutoEnter lock(mCritSect);

  if (mRecording)
  {
    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
                 "  Recording already started");
    return -1;
  }

  if (!mRecordingDeviceIsSpecified)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Recording device is not specified");
    return -1;
  }

  // Nothing needs to be done here, we use a flag to have consistent
  // behavior with other platforms
  mMicIsInitialized = true;

  return 0;
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerVolumeIsAvailable(bool* available)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *available = false;
  return -1;
}

// ----------------------------------------------------------------------------
//  SetSpeakerVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetSpeakerVolume(WebRtc_UWord32 volume)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerVolume(WebRtc_UWord32* volume) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *volume = 0;
  return -1;
}

// ----------------------------------------------------------------------------
//  SetWaveOutVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetWaveOutVolume(WebRtc_UWord16 volumeLeft, WebRtc_UWord16 volumeRight)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  WaveOutVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::WaveOutVolume(WebRtc_UWord16* volumeLeft, WebRtc_UWord16* volumeRight) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::SpeakerIsInitialized() const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return false;
}

// ----------------------------------------------------------------------------
//  MicrophoneIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::MicrophoneIsInitialized() const
{
  CHECK_INITIALIZED_BOOL();
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: %d", mMicIsInitialized);
  return mMicIsInitialized;
}

// ----------------------------------------------------------------------------
//  MaxSpeakerVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MaxSpeakerVolume(WebRtc_UWord32* maxVolume) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *maxVolume = 0;
  return -1;
}

// ----------------------------------------------------------------------------
//  MinSpeakerVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MinSpeakerVolume(WebRtc_UWord32* minVolume) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *minVolume = 0;
  return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerVolumeStepSize
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerVolumeStepSize(WebRtc_UWord16* stepSize) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *stepSize = 0;
  return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerMuteIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerMuteIsAvailable(bool* available)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *available = false;
  return -1;
}

// ----------------------------------------------------------------------------
//  SetSpeakerMute
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetSpeakerMute(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SpeakerMute
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SpeakerMute(bool* enabled) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneMuteIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneMuteIsAvailable(bool* available)
{
  CHECK_INITIALIZED();

  // Mic mute not supported on Gonk
  *available = false;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return 0;
}

// ----------------------------------------------------------------------------
//  SetMicrophoneMute
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetMicrophoneMute(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneMute
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneMute(bool* enabled) const
{
  CHECK_INITIALIZED();

  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneBoostIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneBoostIsAvailable(bool* available)
{
  CHECK_INITIALIZED();

  // Mic boost not supported on Gonk
  *available = false;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return (0);
}

// ----------------------------------------------------------------------------
//  SetMicrophoneBoost
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetMicrophoneBoost(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneBoost
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneBoost(bool* enabled) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneVolumeIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneVolumeIsAvailable(bool* available)
{
  CHECK_INITIALIZED();

  // Mic volume not supported on Gonk
  *available = false;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return (0);
}

// ----------------------------------------------------------------------------
//  SetMicrophoneVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetMicrophoneVolume(WebRtc_UWord32 volume)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneVolume(WebRtc_UWord32* volume) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StereoRecordingIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StereoRecordingIsAvailable(bool* available) const
{
  CHECK_INITIALIZED();

  // Stereo recording not supported on Gonk
  *available = false;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return 0;
}

// ----------------------------------------------------------------------------
//  SetStereoRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetStereoRecording(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StereoRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StereoRecording(bool* enabled) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SetRecordingChannel
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetRecordingChannel(const ChannelType channel)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  RecordingChannel
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RecordingChannel(ChannelType* channel) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StereoPlayoutIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StereoPlayoutIsAvailable(bool* available) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *available = false;
  return -1;
}

// ----------------------------------------------------------------------------
//  SetStereoPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetStereoPlayout(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StereoPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StereoPlayout(bool* enabled) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SetAGC
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetAGC(bool enable)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  AGC
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::AGC() const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return false;
}

// ----------------------------------------------------------------------------
//  PlayoutIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::PlayoutIsAvailable(bool* available)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *available = false;
  return -1;
}

// ----------------------------------------------------------------------------
//  RecordingIsAvailable
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RecordingIsAvailable(bool* available)
{
  CHECK_INITIALIZED();

  *available = false;

  // Try to initialize the playout side
  WebRtc_Word32 res = InitRecording();

  // Cancel effect of initialization
  StopRecording();

  if (res != -1)
  {
    *available = true;
  }

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: available=%d", *available);
  return 0;
}

// ----------------------------------------------------------------------------
//  MaxMicrophoneVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MaxMicrophoneVolume(WebRtc_UWord32* maxVolume) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MinMicrophoneVolume
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MinMicrophoneVolume(WebRtc_UWord32* minVolume) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  MicrophoneVolumeStepSize
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::MicrophoneVolumeStepSize(WebRtc_UWord16* stepSize) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutDevices
// ----------------------------------------------------------------------------

WebRtc_Word16 AudioDeviceGonk::PlayoutDevices()
{
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: #playout devices=%d", 1);
  // There is one device only
  return 1;
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice I (II)
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetPlayoutDevice(WebRtc_UWord16 index)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SetPlayoutDevice II (II)
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetPlayoutDevice(WindowsDeviceType device)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutDeviceName
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::PlayoutDeviceName(
  WebRtc_UWord16 index,
  char name[kAdmMaxDeviceNameSize],
  char guid[kAdmMaxGuidSize])
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);

  // Return empty string
  memset(name, 0, kAdmMaxDeviceNameSize);
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: name=%s", name);

  if (guid)
  {
    memset(guid, 0, kAdmMaxGuidSize);
    WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: guid=%s", guid);
  }

  return -1;
}

// ----------------------------------------------------------------------------
//  RecordingDeviceName
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RecordingDeviceName(
  WebRtc_UWord16 index,
  char name[kAdmMaxDeviceNameSize],
  char guid[kAdmMaxGuidSize])
{
  CHECK_INITIALIZED();

  if (name == NULL)
  {
    mLastError = kAdmErrArgument;
    return -1;
  }

  if (0 != index)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Device index is out of range [0,0]");
    return -1;
  }

  // Return empty string
  memset(name, 0, kAdmMaxDeviceNameSize);
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: name=%s", name);
  if (guid)
  {
    memset(guid, 0, kAdmMaxGuidSize);
    WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: guid=%s", guid);
  }

  return 0;
}

// ----------------------------------------------------------------------------
//  RecordingDevices
// ----------------------------------------------------------------------------

WebRtc_Word16 AudioDeviceGonk::RecordingDevices()
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId,
               "output: #recording devices=%d", 1);
  // There is one device only
  return 1;
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice I (II)
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetRecordingDevice(WebRtc_UWord16 index)
{
  CHECK_INITIALIZED();
  if (mRecIsInitialized)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Recording already initialized");
    return -1;
  }

  // Recording device index will be used for specifying recording
  // audio source, allow any value
  mRecAudioSource = index;
  mRecordingDeviceIsSpecified = true;

  return 0;
}

// ----------------------------------------------------------------------------
//  SetRecordingDevice II (II)
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetRecordingDevice(WindowsDeviceType device)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  InitPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::InitPlayout()
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);

  return -1;
}


static int GetPCMSampleSize(int audioFormat)
{
  switch (audioFormat)
  {
    case AUDIO_FORMAT_PCM_16_BIT:
      return sizeof(int16_t);
    case AUDIO_FORMAT_PCM_8_BIT:
      return sizeof(int8_t);
    case AUDIO_FORMAT_PCM_32_BIT:
    case AUDIO_FORMAT_PCM_8_24_BIT:
      return sizeof(int32_t);
    default:
      return -1;
  }
}

static int GetMinRecordBufferSize(int sampleRate, int nbChannels, int audioFormat)
{
  int frameCount = 0;
  status_t result = AudioRecord::getMinFrameCount(&frameCount,
                                                  sampleRate,
                                                  audioFormat,
                                                  nbChannels);

  if (result == BAD_VALUE) {
    return 0;
  }
  if (result != NO_ERROR) {
    return -1;
  }
  return frameCount * nbChannels * GetPCMSampleSize(audioFormat);
}

// ----------------------------------------------------------------------------
//  InitRecording
// ----------------------------------------------------------------------------
int AudioDeviceGonk::InitRecorderLocked(int audioSource, int sampleRate)
{
  // get the minimum buffer size that can be used
  int minRecBufSize = GetMinRecordBufferSize(sampleRate,
                                             N_REC_CHANNELS,
                                             N_AUDIO_FORMAT);

  // create a bigger buffer to store PCM data, to avoid data loss if
  // polling thread runs to slow
  int recBufSize = minRecBufSize * 2;
  mBufferedRecSamples = (5 * sampleRate) / 200;

  // release the object
  if (mAudioRecord != NULL) {
    mAudioRecord->stop();
    delete mAudioRecord;
    mAudioRecord = NULL;
  }

  unsigned int frameCount =
    minRecBufSize / (N_REC_CHANNELS * GetPCMSampleSize(N_AUDIO_FORMAT));
  int channelMask = N_REC_CHANNELS == 1 ?
                    AUDIO_CHANNEL_IN_MONO : AUDIO_CHANNEL_IN_STEREO;

  mAudioRecord = new AudioRecord();
  mAudioRecord->set(audioSource,     // inputSource
                    sampleRate,      // sampleRate
                    N_AUDIO_FORMAT,  // format
                    channelMask,     // channelMask
                    frameCount,      // frameCount
                    0,               // flags
                    NULL,            // cbf
                    this,            // user
                    0,               // notificationFrames
                    false,           // threadCanCallJava
                    0);              // sessionId

  if (mAudioRecord->initCheck() != NO_ERROR) {
    return -1;
  }

  return mBufferedRecSamples;
}

WebRtc_Word32 AudioDeviceGonk::InitRecording()
{
  CHECK_INITIALIZED();
  ReentrantMonitorAutoEnter lock(mCritSect);
  mAudioDeviceBuffer.InitRecording();

  if (!mInitialized)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Not initialized");
    return -1;
  }

  if (mRecording)
  {
    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
                 "  Recording already started");
    return -1;
  }

  if (!mRecordingDeviceIsSpecified)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Recording device is not specified");
    return -1;
  }

  if (mRecIsInitialized)
  {
    WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, mId,
                 "  Recording already initialized");
    return 0;
  }

  // Initialize the microphone
  if (InitMicrophone() == -1)
  {
    WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
                 "  InitMicrophone() failed");
  }

  int samplingFreq = 44100;
  if (mSamplingFreqIn != 44)
  {
    samplingFreq = mSamplingFreqIn * 1000;
  }

  int retVal = -1;
  int res = InitRecorderLocked(mRecAudioSource, samplingFreq);
  if (res < 0)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "InitRecording failed (%d)", res);
  }
  else
  {
    // Set the audio device buffer sampling rate
    mAudioDeviceBuffer.SetRecordingSampleRate(mSamplingFreqIn * 1000);

    // the init rec function returns a fixed delay
    mDelayRecording = res / mSamplingFreqIn;

    mRecIsInitialized = true;
    retVal = 0;
  }

  return retVal;
}

// ----------------------------------------------------------------------------
//  PlayoutIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::PlayoutIsInitialized() const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return false;
}

// ----------------------------------------------------------------------------
//  RecordingIsInitialized
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::RecordingIsInitialized() const
{
  CHECK_INITIALIZED_BOOL();
  return mRecIsInitialized;
}

// ----------------------------------------------------------------------------
//  StartPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StartPlayout()
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StopPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StopPlayout()
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  Playing
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::Playing() const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  StartRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StartRecording()
{
  CHECK_INITIALIZED();

  ReentrantMonitorAutoEnter lock(mCritSect);

  if (!mRecIsInitialized)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Recording not initialized");
    return -1;
  }

  if (mRecording)
  {
    WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, mId,
                 "  Recording already started");
    return 0;
  }

  mAudioRecord->start();

  mRecWarning = 0;
  mRecError = 0;

  // Signal to recording thread that we want to start
  mStartRec = true;
  mTimeEventRec.Set(); // Release thread from waiting state
  UnLock();
  // Wait for thread to init
  if (kEventSignaled != mRecStartStopEvent.Wait(5000))
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Timeout or error starting");
  }
  mRecStartStopEvent.Reset();
  Lock();

  return 0;
}
// ----------------------------------------------------------------------------
//  StopRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StopRecording()
{
  CHECK_INITIALIZED();
  ReentrantMonitorAutoEnter lock(mCritSect);

  if (!mRecIsInitialized)
  {
    WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, mId,
                 "  Recording is not initialized");
    return 0;
  }

  // make sure we don't start recording (it's asynchronous),
  // assuming that we are under lock
  mStartRec = false;

  mAudioRecord->stop();

  mRecIsInitialized = false;
  mRecording = false;
  mRecWarning = 0;
  mRecError = 0;

  return 0;
}

// ----------------------------------------------------------------------------
//  Recording
// ----------------------------------------------------------------------------

bool AudioDeviceGonk::Recording() const
{
  CHECK_INITIALIZED_BOOL();
  ReentrantMonitorAutoEnter lock(mCritSect);
  return mRecording;
}

// ----------------------------------------------------------------------------
//  RegisterEventObserver
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RegisterEventObserver(AudioDeviceObserver* eventCallback)
{
  ReentrantMonitorAutoEnter lock(mCritSectEventCb);
  mPtrCbAudioDeviceObserver = eventCallback;

  return 0;
}

// ----------------------------------------------------------------------------
//  RegisterAudioCallback
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RegisterAudioCallback(AudioTransport* audioCallback)
{
  ReentrantMonitorAutoEnter lock(mCritSectAudioCb);
  mAudioDeviceBuffer.RegisterAudioCallback(audioCallback);

  return 0;
}

// ----------------------------------------------------------------------------
//  StartRawInputFileRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StartRawInputFileRecording(
  const char pcmFileNameUTF8[kAdmMaxFileNameSize])
{
  CHECK_INITIALIZED();

  if (NULL == pcmFileNameUTF8)
  {
    return -1;
  }

  return mAudioDeviceBuffer.StartInputFileRecording(pcmFileNameUTF8);
}

// ----------------------------------------------------------------------------
//  StopRawInputFileRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StopRawInputFileRecording()
{
  CHECK_INITIALIZED();

  return mAudioDeviceBuffer.StopInputFileRecording();
}

// ----------------------------------------------------------------------------
//  StartRawOutputFileRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StartRawOutputFileRecording(
    const char pcmFileNameUTF8[kAdmMaxFileNameSize])
{
  CHECK_INITIALIZED();

  if (NULL == pcmFileNameUTF8)
  {
    return -1;
  }

  return mAudioDeviceBuffer.StartOutputFileRecording(pcmFileNameUTF8);
}

// ----------------------------------------------------------------------------
//  StopRawOutputFileRecording
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::StopRawOutputFileRecording()
{
  CHECK_INITIALIZED();

  return mAudioDeviceBuffer.StopOutputFileRecording();
}

// ----------------------------------------------------------------------------
//  SetPlayoutBuffer
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetPlayoutBuffer(const BufferType type, WebRtc_UWord16 sizeMS)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutBuffer
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::PlayoutBuffer(BufferType* type, WebRtc_UWord16* sizeMS) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *type = AudioDeviceModule::kAdaptiveBufferSize;
  *sizeMS = 0;
  return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutDelay
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::PlayoutDelay(WebRtc_UWord16* delayMS) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  *delayMS = 0;
  return 0;
}

// ----------------------------------------------------------------------------
//  RecordingDelay
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RecordingDelay(WebRtc_UWord16* delayMS) const
{
  WEBRTC_TRACE(kTraceStream, kTraceAudioDevice, mId, "%s", __FUNCTION__);
  CHECK_INITIALIZED();
  ReentrantMonitorAutoEnter lock(mCritSect);

  *delayMS = mDelayRecording;

  WEBRTC_TRACE(kTraceStream, kTraceAudioDevice, mId, "output: delayMS=%u", *delayMS);
  return 0;
}

// ----------------------------------------------------------------------------
//  CPULoad
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::CPULoad(WebRtc_UWord16* load) const
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  SetRecordingSampleRate
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetRecordingSampleRate(const WebRtc_UWord32 samplesPerSec)
{
  CHECK_INITIALIZED();

  if (samplesPerSec > 48000 || samplesPerSec < 8000)
  {
    WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                 "  Invalid sample rate");
    return -1;
  }

  // set the recording sample rate to use
  mSamplingFreqIn = samplesPerSec / 1000;

  // Update the AudioDeviceBuffer
  mAudioDeviceBuffer.SetRecordingSampleRate(samplesPerSec);

  return 0;
}

// ----------------------------------------------------------------------------
//  RecordingSampleRate
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::RecordingSampleRate(WebRtc_UWord32* samplesPerSec) const
{
  CHECK_INITIALIZED();

  WebRtc_Word32 sampleRate = mAudioDeviceBuffer.RecordingSampleRate();

  if (sampleRate == -1)
  {
      WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId, "failed to retrieve the sample rate");
      return -1;
  }

  *samplesPerSec = sampleRate;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId, "output: samplesPerSec=%u", *samplesPerSec);
  return (0);
}

// ----------------------------------------------------------------------------
//  SetPlayoutSampleRate
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetPlayoutSampleRate(const WebRtc_UWord32 samplesPerSec)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  PlayoutSampleRate
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::PlayoutSampleRate(WebRtc_UWord32* samplesPerSec) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  ResetAudioDevice
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::ResetAudioDevice()
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
      "Reset audio device not supported on this platform");
  return -1;
}

// ----------------------------------------------------------------------------
//  SetLoudspeakerStatus
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::SetLoudspeakerStatus(bool enable)
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  return -1;
}

// ----------------------------------------------------------------------------
//  GetLoudspeakerStatus
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::GetLoudspeakerStatus(bool* enabled) const
{
  WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice, mId,
               "  API call not supported on this platform: %s", __FUNCTION__);
  enabled = false;
  return -1;
}

int32_t AudioDeviceGonk::EnableBuiltInAEC(bool enable)
{
  CHECK_INITIALIZED();
  WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
      "Windows AEC not supported on this platform");
  return -1;
}

bool AudioDeviceGonk::BuiltInAECIsEnabled() const
{
  CHECK_INITIALIZED_BOOL();
  WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
      "Windows AEC not supported on this platform");
  return false;
}

// ============================================================================
//                                 Private Methods
// ============================================================================

// ----------------------------------------------------------------------------
//  InitSampleRate
//
//  checks supported sample rates for playback
//  and recording and initializes the rates to be used
//  Also stores the max playout volume returned from InitPlayout
// ----------------------------------------------------------------------------

WebRtc_Word32 AudioDeviceGonk::InitSampleRate()
{
  ReentrantMonitorAutoEnter lock(mCritSect);
  int samplingFreq = 48000;
  int res = 0;

  if (mSamplingFreqIn > 0)
  {
    // read the configured sampling rate
    samplingFreq = 44100;
    if (mSamplingFreqIn != 44)
    {
        samplingFreq = mSamplingFreqIn * 1000;
    }
    WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId,
                 "  Trying configured recording sampling rate %d",
                 samplingFreq);
  }

  res = InitRecorderLocked(mRecAudioSource, samplingFreq);
  // According to Android SDK, 44100 is guaranteed to work on all devices,
  // when error occurs here, default to 44100
  if (res < 0)
  {
    if (samplingFreq == 48000)
    {
      res = InitRecorderLocked(mRecAudioSource, 44100);
    }
    if (res < 0)
    {
      WEBRTC_TRACE(kTraceError,
                   kTraceAudioDevice, mId,
                   "%s: InitRecording failed (%d)", __FUNCTION__,
                   res);
    }
    return -1;
  }

  // set the recording sample rate to use
  mSamplingFreqIn = samplingFreq / 1000;

  WEBRTC_TRACE(kTraceStateInfo, kTraceAudioDevice, mId,
               "Recording sample rate set to (%d)", mSamplingFreqIn);

  mAudioRecord->stop();
  return 0;
}

void AudioDeviceGonk::RecThreadFunc() {
  Lock();
  if (!mRecThreadIsInitialized)
  {
    mRecThreadIsInitialized = true;
  }

  // just sleep if rec has not started
  if (!mRecording)
  {
    UnLock();
    switch (mTimeEventRec.Wait(1000))
    {
      case kEventSignaled:
        WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice,
                     mId, "Recording thread event signal");
        mTimeEventRec.Reset();
        break;
      case kEventError:
        WEBRTC_TRACE(kTraceWarning, kTraceAudioDevice,
                     mId, "Recording thread event error");
        mThreadRec->Dispatch(WrapRunnable(this, &AudioDeviceGonk::RecThreadFunc), NS_DISPATCH_NORMAL);
        return;
      case kEventTimeout:
        WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice,
                     mId, "Recording thread event timeout");
        mThreadRec->Dispatch(WrapRunnable(this, &AudioDeviceGonk::RecThreadFunc), NS_DISPATCH_NORMAL);
        return;
    }
    Lock();
  }

  if (mStartRec)
  {
    WEBRTC_TRACE(kTraceInfo, kTraceAudioDevice, mId,
                 "_startRec true, performing initial actions");
    mStartRec = false;
    mRecording = true;
    mRecWarning = 0;
    mRecError = 0;
    mRecStartStopEvent.Set();
  }

  if (mRecording)
  {
    WebRtc_UWord32 samplesToRec = mSamplingFreqIn * 10;

    // Copy data to our direct buffer
    UnLock();
    int sizeInBytes = GetPCMSampleSize(N_AUDIO_FORMAT) * samplesToRec;
    int recorderBuffSize = mAudioRecord->frameCount() * mAudioRecord->frameSize();
    int readSize = mAudioRecord->read(mRecBuffer, sizeInBytes > recorderBuffSize ? recorderBuffSize : sizeInBytes);
    Lock();
    if (readSize <= 0)
    {
      WEBRTC_TRACE(kTraceError, kTraceAudioDevice, mId,
                   "read AudioRecord failed(%d)", readSize);
      mRecError = readSize;
    }

    // store the recorded buffer (no action will be taken if the
    // #recorded samples is not a full buffer)
    mAudioDeviceBuffer.SetRecordedBuffer(mRecBuffer, samplesToRec);

    // store vqe delay values
    mAudioDeviceBuffer.SetVQEData(0, mDelayRecording, 0);

    // deliver recorded samples at specified sample rate, mic level
    // etc. to the observer using callback
    UnLock();
    mAudioDeviceBuffer.DeliverRecordedData();
    Lock();
  } // _recording

  if (mShutdownRecThread)
  {
    WEBRTC_TRACE(kTraceDebug, kTraceAudioDevice, mId,
                 "Detaching rec thread from Java VM");

    mShutdownRecThread = false;
    mRecStartStopEvent.Set();
  }

  UnLock();
  mThreadRec->Dispatch(WrapRunnable(this, &AudioDeviceGonk::RecThreadFunc), NS_DISPATCH_NORMAL);
  return;
}

}  // namespace webrtc
