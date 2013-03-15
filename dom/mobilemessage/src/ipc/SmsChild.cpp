/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SmsChild.h"
#include "SmsMessage.h"
#include "MmsMessage.h"
#include "Constants.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "mozilla/dom/ContentChild.h"
#include "SmsRequest.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::mobilemessage;

namespace {

void
NotifyObserversWithMobileMessage(const char* aEventName,
                                 const MobileMessageData& aMsgData)
{
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  nsCOMPtr<nsISupports> msg;
  switch (aMsgData.type()) {
  case MobileMessageData::TMmsMessageData:
    msg = new MmsMessage(aMsgData.get_MmsMessageData());
    break;
  case MobileMessageData::TSmsMessageData:
    msg = new SmsMessage(aMsgData.get_SmsMessageData());
    break;
  default:
    MOZ_NOT_REACHED("Received invalid response parameters!");
    return;
  }

  obs->NotifyObservers(msg, aEventName, nullptr);
}

} // anonymous namespace

namespace mozilla {
namespace dom {
namespace mobilemessage {

SmsChild::SmsChild()
{
  MOZ_COUNT_CTOR(SmsChild);
}

SmsChild::~SmsChild()
{
  MOZ_COUNT_DTOR(SmsChild);
}

void
SmsChild::ActorDestroy(ActorDestroyReason aWhy)
{
}

bool
SmsChild::RecvNotifyReceivedMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsReceivedObserverTopic, aMessageData);
  return true;
}

bool
SmsChild::RecvNotifySendingMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsSendingObserverTopic, aMessageData);
  return true;
}

bool
SmsChild::RecvNotifySentMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsSentObserverTopic, aMessageData);
  return true;
}

bool
SmsChild::RecvNotifyFailedMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsFailedObserverTopic, aMessageData);
  return true;
}

bool
SmsChild::RecvNotifyDeliverySuccessMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsDeliverySuccessObserverTopic, aMessageData);
  return true;
}

bool
SmsChild::RecvNotifyDeliveryErrorMessage(const MobileMessageData& aMessageData)
{
  NotifyObserversWithMobileMessage(kSmsDeliveryErrorObserverTopic, aMessageData);
  return true;
}

PSmsRequestChild*
SmsChild::AllocPSmsRequest(const IPCSmsRequest& aRequest)
{
  MOZ_NOT_REACHED("Caller is supposed to manually construct a request!");
  return nullptr;
}

bool
SmsChild::DeallocPSmsRequest(PSmsRequestChild* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * SmsRequestChild
 ******************************************************************************/

SmsRequestChild::SmsRequestChild(nsIMobileMessageCallback* aReplyRequest)
: mReplyRequest(aReplyRequest)
{
  MOZ_COUNT_CTOR(SmsRequestChild);
  MOZ_ASSERT(aReplyRequest);
}

SmsRequestChild::~SmsRequestChild()
{
  MOZ_COUNT_DTOR(SmsRequestChild);
}

void
SmsRequestChild::ActorDestroy(ActorDestroyReason aWhy)
{
  // Nothing needed here.
}

already_AddRefed<nsISupports>
SmsRequestChild::GetMessageFromMobileMessageData(const MobileMessageData& aData)
{
  nsCOMPtr<nsISupports> mMsg;
  switch(aData. type()) {
  case MobileMessageData::TMmsMessageData:
    mMsg = new MmsMessage(aData.get_MmsMessageData());
    break;
  case MobileMessageData::TSmsMessageData:
    mMsg = new SmsMessage(aData.get_SmsMessageData());
    break;
  default:
    MOZ_NOT_REACHED("Unexpected type of MobileMessageData");
    return nullptr;
  }
  return mMsg.forget();
}

bool
SmsRequestChild::Recv__delete__(const MessageReply& aReply)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mReplyRequest);
  nsCOMPtr<SmsMessage> message;
  switch(aReply.type()) {
    case MessageReply::TReplyMessageSend:
      {
        nsCOMPtr<nsISupports> msg = GetMessageFromMobileMessageData(aReply.get_ReplyMessageSend().messageData());
        mReplyRequest->NotifyMessageSent(msg.get());
      }
      break;
    case MessageReply::TReplyMessageSendFail:
      mReplyRequest->NotifySendMessageFailed(aReply.get_ReplyMessageSendFail().error());
      break;
    case MessageReply::TReplyGetMessage:
      message = new SmsMessage(aReply.get_ReplyGetMessage().messageData());
      mReplyRequest->NotifyMessageGot(message);
      break;
    case MessageReply::TReplyGetMessageFail:
      mReplyRequest->NotifyGetMessageFailed(aReply.get_ReplyGetMessageFail().error());
      break;
    case MessageReply::TReplyMessageDelete:
      mReplyRequest->NotifyMessageDeleted(aReply.get_ReplyMessageDelete().deleted());
      break;
    case MessageReply::TReplyMessageDeleteFail:
      mReplyRequest->NotifyMessageDeleted(aReply.get_ReplyMessageDeleteFail().error());
      break;
    case MessageReply::TReplyNoMessageInList:
      mReplyRequest->NotifyNoMessageInList();
      break;
    case MessageReply::TReplyCreateMessageList:
      message = new SmsMessage(aReply.get_ReplyCreateMessageList().messageData());
      mReplyRequest->NotifyMessageListCreated(aReply.get_ReplyCreateMessageList().listId(), 
                                              message);
      break;
    case MessageReply::TReplyCreateMessageListFail:
      mReplyRequest->NotifyReadMessageListFailed(aReply.get_ReplyCreateMessageListFail().error());
      break;
    case MessageReply::TReplyGetNextMessage:
      message = new SmsMessage(aReply.get_ReplyGetNextMessage().messageData());
      mReplyRequest->NotifyNextMessageInListGot(message);
      break;
    case MessageReply::TReplyMarkeMessageRead:
      mReplyRequest->NotifyMessageMarkedRead(aReply.get_ReplyMarkeMessageRead().read());
      break;
    case MessageReply::TReplyMarkeMessageReadFail:
      mReplyRequest->NotifyMarkMessageReadFailed(aReply.get_ReplyMarkeMessageReadFail().error());
      break;
    case MessageReply::TReplyThreadList: {
      SmsRequestForwarder* forwarder = static_cast<SmsRequestForwarder*>(mReplyRequest.get());
      SmsRequest* request = static_cast<SmsRequest*>(forwarder->GetRealRequest());
      request->NotifyThreadList(aReply.get_ReplyThreadList().items());
    } break;
    case MessageReply::TReplyThreadListFail:
      mReplyRequest->NotifyThreadListFailed(aReply.get_ReplyThreadListFail().error());
      break;
    default:
      MOZ_NOT_REACHED("Received invalid response parameters!");
      return false;
  }

  return true;
}

} // namespace mobilemessage
} // namespace dom
} // namespace mozilla
