/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FAKE_MEDIA_STREAM_H_
#define FAKE_MEDIA_STREAM_H_

#include <set>

#include "nsNetCID.h"
#include "nsITimer.h"
#include "nsComponentManagerUtils.h"
#include "nsIComponentManager.h"
#include "nsIComponentRegistrar.h"

// #includes from MediaStream.h
#include "mozilla/Mutex.h"
#include "AudioSegment.h"
#include "MediaSegment.h"
#include "StreamBuffer.h"
#include "nsTArray.h"
#include "nsIRunnable.h"
#include "nsISupportsImpl.h"
#include "nsIDOMMediaStream.h"

class nsIDOMWindow;

namespace mozilla {
   class MediaStreamGraph;
   class MediaSegment;
};

class Fake_SourceMediaStream;

static const int64_t USECS_PER_S = 1000000;

class Fake_MediaStreamListener
{
 public:
  virtual ~Fake_MediaStreamListener() {}

  virtual void NotifyQueuedTrackChanges(mozilla::MediaStreamGraph* aGraph, mozilla::TrackID aID,
                                        mozilla::TrackRate aTrackRate,
                                        mozilla::TrackTicks aTrackOffset,
                                        uint32_t aTrackEvents,
                                        const mozilla::MediaSegment& aQueuedMedia)  = 0;
  virtual void NotifyPull(mozilla::MediaStreamGraph* aGraph, mozilla::StreamTime aDesiredTime) = 0;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Fake_MediaStreamListener)
};

// Note: only one listener supported
class Fake_MediaStream {
 public:
  Fake_MediaStream () : mListeners(), mMutex("Fake MediaStream") {}
  virtual ~Fake_MediaStream() { Stop(); }

  void AddListener(Fake_MediaStreamListener *aListener) {
    mozilla::MutexAutoLock lock(mMutex);
    mListeners.insert(aListener);
  }

  void RemoveListener(Fake_MediaStreamListener *aListener) {
    mozilla::MutexAutoLock lock(mMutex);
    mListeners.erase(aListener);
  }

  virtual Fake_SourceMediaStream *AsSourceStream() { return NULL; }

  virtual nsresult Start() { return NS_OK; }
  virtual nsresult Stop() { return NS_OK; }

  virtual void Periodic() {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Fake_MediaStream);

 protected:
  std::set<Fake_MediaStreamListener *> mListeners;
  mozilla::Mutex mMutex;  // Lock to prevent the listener list from being modified while
  		 	  // executing Periodic().
};

class Fake_MediaPeriodic : public nsITimerCallback {
public:
Fake_MediaPeriodic(Fake_MediaStream *aStream) : mStream(aStream),
                                                mCount(0) {}
  virtual ~Fake_MediaPeriodic() {}
  void Detach() {
    mStream = NULL;
  }

  int GetTimesCalled() { return mCount; }

  NS_DECL_ISUPPORTS
  NS_DECL_NSITIMERCALLBACK

protected:
  Fake_MediaStream *mStream;
  int mCount;
};


class Fake_SourceMediaStream : public Fake_MediaStream {
 public:
  Fake_SourceMediaStream() : mSegmentsAdded(0),
                             mDesiredTime(0),
                             mPullEnabled(false),
                             mStop(false),
                             mPeriodic(new Fake_MediaPeriodic(this)) {}

  void AddTrack(mozilla::TrackID aID, mozilla::TrackRate aRate, mozilla::TrackTicks aStart,
                mozilla::MediaSegment* aSegment) {}
  void EndTrack(mozilla::TrackID aID) {}

  bool AppendToTrack(mozilla::TrackID aID, mozilla::MediaSegment* aSegment) {
    bool nonZeroSample = false;
    MOZ_ASSERT(aSegment);
    if(aSegment->GetType() == mozilla::MediaSegment::AUDIO) {
      //On audio segment append, we verify for validity
      //of the audio samples.
      mozilla::AudioSegment* audio =
              static_cast<mozilla::AudioSegment*>(aSegment);
      mozilla::AudioSegment::ChunkIterator iter(*audio);
      while(!iter.IsEnded()) {
        mozilla::AudioChunk& chunk = *(iter);
        MOZ_ASSERT(chunk.mBuffer);
        const int16_t* buf =
          static_cast<const int16_t*>(chunk.mChannelData[0]);
        for(int i=0; i<chunk.mDuration; i++) {
          if(buf[i]) {
            //atleast one non-zero sample found.
            nonZeroSample = true; 
            break;
          }
        }
        //process next chunk
        iter.Next();
      }
      if(nonZeroSample) {
          //we increment segments count if
          //atleast one non-zero samples was found.
          ++mSegmentsAdded;
      }
    } else {
      //in the case of video segment appended, we just increase the
      //segment count.
      ++mSegmentsAdded;
    }
    return true;
  }

  void AdvanceKnownTracksTime(mozilla::StreamTime aKnownTime) {}

  void SetPullEnabled(bool aEnabled) {
    mPullEnabled = aEnabled;
  }
  //Don't pull anymore data,if mStop is true.
  void StopStream() {
   mStop = true;
  }

  virtual Fake_SourceMediaStream *AsSourceStream() { return this; }

  virtual nsresult Start();
  virtual nsresult Stop();

  virtual void Periodic();

  virtual int GetSegmentsAdded() {
    return mSegmentsAdded;
  }

 protected:
  int mSegmentsAdded;
  uint64_t mDesiredTime;
  bool mPullEnabled;
  bool mStop;
  nsRefPtr<Fake_MediaPeriodic> mPeriodic;
  nsCOMPtr<nsITimer> mTimer;
};


class Fake_DOMMediaStream : public nsIDOMMediaStream
{
public:
  Fake_DOMMediaStream() : mMediaStream(new Fake_MediaStream()) {}
  Fake_DOMMediaStream(Fake_MediaStream *stream) :
      mMediaStream(stream) {}

  virtual ~Fake_DOMMediaStream() {
    // Note: memory leak
    mMediaStream->Stop();
  }

  NS_DECL_ISUPPORTS

  static already_AddRefed<Fake_DOMMediaStream>
  CreateSourceStream(nsIDOMWindow* aWindow, uint32_t aHintContents) {
    Fake_SourceMediaStream *source = new Fake_SourceMediaStream();

    nsRefPtr<Fake_DOMMediaStream> ds = new Fake_DOMMediaStream(source);
    ds->SetHintContents(aHintContents);

    return ds.forget();
  }

  Fake_MediaStream *GetStream() { return mMediaStream; }

  // Hints to tell the SDP generator about whether this
  // MediaStream probably has audio and/or video
  enum {
    HINT_CONTENTS_AUDIO = 0x00000001U,
    HINT_CONTENTS_VIDEO = 0x00000002U
  };
  uint32_t GetHintContents() const { return mHintContents; }
  void SetHintContents(uint32_t aHintContents) { mHintContents = aHintContents; }

private:
  nsRefPtr<Fake_MediaStream> mMediaStream;

  // tells the SDP generator about whether this
  // MediaStream probably has audio and/or video
  uint32_t mHintContents;
};

class Fake_MediaStreamGraph
{
public:
  virtual ~Fake_MediaStreamGraph() {}
};



class Fake_MediaStreamBase : public Fake_MediaStream {
 public:
  Fake_MediaStreamBase() : mPeriodic(new Fake_MediaPeriodic(this)) {}

  virtual nsresult Start();
  virtual nsresult Stop();


  virtual int GetSegmentsAdded() {
    return mPeriodic->GetTimesCalled();
  }

 private:
  nsCOMPtr<nsITimer> mTimer;
  nsRefPtr<Fake_MediaPeriodic> mPeriodic;
};


class Fake_AudioStreamSource : public Fake_MediaStreamBase {
 public:
  Fake_AudioStreamSource() : Fake_MediaStreamBase(),
                             mCount(0),
                             mStop(false) {}
  //Signaling Agent indicates us to stop generating
  //further audio.
  void StopStream() {
    mStop = true;
  }
  virtual void Periodic();
  int mCount;
  bool mStop;
};

class Fake_VideoStreamSource : public Fake_MediaStreamBase {
 public:
  Fake_VideoStreamSource() : Fake_MediaStreamBase() {}
};


namespace mozilla {
typedef Fake_MediaStream MediaStream;
typedef Fake_SourceMediaStream SourceMediaStream;
typedef Fake_MediaStreamListener MediaStreamListener;
typedef Fake_DOMMediaStream DOMMediaStream;
}

#endif
