/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SmsParent.h"
#include "nsISmsService.h"
#include "nsIMmsService.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "Constants.h"
#include "nsIDOMMozSmsMessage.h"
#include "nsIDOMMozMmsMessage.h"
#include "mozilla/unused.h"
#include "SmsMessage.h"
#include "MmsMessage.h"
#include "nsIMobileMessageDatabaseService.h"
#include "SmsFilter.h"
#include "SmsRequest.h"
#include "SmsSegmentInfo.h"
#include "mozilla/dom/ipc/Blob.h"
#include "nsIDOMFile.h"
#include "mozilla/dom/mobilemessage/MobileMessageCallback.h"
#include "mozilla/dom/ContentParent.h"

namespace mozilla {
namespace dom {
namespace mobilemessage {

static JSObject*
MmsAttachmentDataToJSObject(JSContext* aContext,
                            const MmsAttachmentData& aAttachment)
{
  JSObject* obj = JS_NewObject(aContext, nullptr, nullptr, nullptr);
  NS_ENSURE_TRUE(obj, nullptr);

  // id
  JSString* idStr = JS_NewUCStringCopyN(aContext,
                                        aAttachment.id().get(),
                                        aAttachment.id().Length());
  NS_ENSURE_TRUE(idStr, nullptr);
  if (!JS_DefineProperty(aContext, obj, "id", JS::StringValue(idStr),
                         nullptr, nullptr, 0)) {
    return nullptr;
  }
  // location
  JSString* locStr = JS_NewUCStringCopyN(aContext,
                                         aAttachment.location().get(),
                                         aAttachment.location().Length());
  NS_ENSURE_TRUE(locStr, nullptr);
  if (!JS_DefineProperty(aContext, obj, "location", JS::StringValue(locStr),
                         nullptr, nullptr, 0)) {
    return nullptr;
  }

  // content.
  nsCOMPtr<nsIDOMBlob> blob = static_cast<BlobParent*>(aAttachment.contentParent())->GetBlob();
  JS::Value content;
  nsresult rv = nsContentUtils::WrapNative(aContext,
                                           JS_GetGlobalForScopeChain(aContext),
                                           blob,
                                           &NS_GET_IID(nsIDOMBlob),
                                           &content);
  NS_ENSURE_SUCCESS(rv, nullptr);
  if (!JS_DefineProperty(aContext, obj, "content", content,
                         nullptr, nullptr, 0)) {
    return nullptr;
  }

  return obj;
}

static bool
GetParamsFromSendMmsMessageRequest(const SendMmsMessageRequest& aRequest,
                                   JS::Value* aParam)
{
  AutoJSContext cx;
  JSObject* paramsObj = JS_NewObject(cx, nullptr, nullptr, nullptr);
  NS_ENSURE_TRUE(paramsObj, false);

  // smil
  JSString* smilStr = JS_NewUCStringCopyN(cx,
                                          aRequest.smil().get(),
                                          aRequest.smil().Length());
  NS_ENSURE_TRUE(smilStr, false);
  if(!JS_DefineProperty(cx, paramsObj, "smil", JS::StringValue(smilStr),
                        nullptr, nullptr, 0)) {
    return false;
  }

  // subject
  JSString* subjectStr = JS_NewUCStringCopyN(cx,
                                             aRequest.subject().get(),
                                             aRequest.subject().Length());
  NS_ENSURE_TRUE(subjectStr, false);
  if(!JS_DefineProperty(cx, paramsObj, "subject",
                        JS::StringValue(subjectStr), nullptr, nullptr, 0)) {
    return false;
  }

  // receivers
  JSObject* receiverArray = JS_NewArrayObject(cx,
                                              aRequest.receivers().Length(),
                                              nullptr);
  NS_ENSURE_TRUE(receiverArray, false);
  for (uint32_t i = 0; i < aRequest.receivers().Length(); i++) {
    const nsString &recv = aRequest.receivers().ElementAt(i);
    JSString* jsStr = JS_NewUCStringCopyN(cx,
                                          recv.get(),
                                          recv.Length());
    NS_ENSURE_TRUE(jsStr, false);
    jsval val = JS::StringValue(jsStr);
    if (!JS_SetElement(cx, receiverArray, i, &val)) {
      return false;
    }
  }
  if (!JS_DefineProperty(cx, paramsObj, "receivers",
                         JS::ObjectValue(*receiverArray), nullptr, nullptr, 0)) {
    return false;
  }

  // attachments
  JSObject* attachmentArray = JS_NewArrayObject(cx,
                                                aRequest.attachments().Length(),
                                                nullptr);
  for (uint32_t i = 0; i < aRequest.attachments().Length(); i++) {
    JSObject *obj = MmsAttachmentDataToJSObject(cx,
                                                aRequest.attachments().ElementAt(i));
    NS_ENSURE_TRUE(obj, false);
    jsval val = JS::ObjectValue(*obj);
    if (!JS_SetElement(cx, attachmentArray, i, &val)) {
      return false;
    }
  }

  if (!JS_DefineProperty(cx, paramsObj, "attachments",
                         JS::ObjectValue(*attachmentArray), nullptr, nullptr, 0)) {
    return false;
  }

  aParam->setObject(*paramsObj);
  return true;
}

NS_IMPL_ISUPPORTS1(SmsParent, nsIObserver)

SmsParent::SmsParent()
{
  MOZ_COUNT_CTOR(SmsParent);
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  obs->AddObserver(this, kSmsReceivedObserverTopic, false);
  obs->AddObserver(this, kSmsSendingObserverTopic, false);
  obs->AddObserver(this, kSmsSentObserverTopic, false);
  obs->AddObserver(this, kSmsFailedObserverTopic, false);
  obs->AddObserver(this, kSmsDeliverySuccessObserverTopic, false);
  obs->AddObserver(this, kSmsDeliveryErrorObserverTopic, false);
}

SmsParent::~SmsParent()
{
  MOZ_COUNT_DTOR(SmsParent);
}

void
SmsParent::ActorDestroy(ActorDestroyReason why)
{
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    return;
  }

  obs->RemoveObserver(this, kSmsReceivedObserverTopic);
  obs->RemoveObserver(this, kSmsSendingObserverTopic);
  obs->RemoveObserver(this, kSmsSentObserverTopic);
  obs->RemoveObserver(this, kSmsFailedObserverTopic);
  obs->RemoveObserver(this, kSmsDeliverySuccessObserverTopic);
  obs->RemoveObserver(this, kSmsDeliveryErrorObserverTopic);
}

NS_IMETHODIMP
SmsParent::Observe(nsISupports* aSubject, const char* aTopic,
                   const PRUnichar* aData)
{
  if (!strcmp(aTopic, kSmsReceivedObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-received' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyReceivedMessage(msgData);
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsSendingObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-sending' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifySendingMessage(msgData);
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsSentObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-sent' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifySentMessage(msgData);
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsFailedObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-failed' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyFailedMessage(msgData);
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsDeliverySuccessObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-sending' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyDeliverySuccessMessage(msgData);
    return NS_OK;
  }

  if (!strcmp(aTopic, kSmsDeliveryErrorObserverTopic)) {
    MobileMessageData msgData;
    if (!GetMobileMessageDataFromMessage(aSubject, msgData)) {
      NS_ERROR("Got a 'sms-delivery-error' topic without a valid message!");
      return NS_OK;
    }

    unused << SendNotifyDeliveryErrorMessage(msgData);
    return NS_OK;
  }

  return NS_OK;
}

bool
SmsParent::GetMobileMessageDataFromMessage(nsISupports *aMsg,
                                           MobileMessageData &aData)
{
  nsCOMPtr<nsIDOMMozMmsMessage> mmsMsg = do_QueryInterface(aMsg);
  if (mmsMsg) {
    MmsMessageData data;
    ContentParent *parent = static_cast<ContentParent*>(Manager());
    static_cast<MmsMessage*>(mmsMsg.get())->GetMmsMessageData(parent, &data);
    aData = data;
    return true;
  }

  nsCOMPtr<nsIDOMMozSmsMessage> smsMsg = do_QueryInterface(aMsg);
  if (smsMsg) {
    aData = static_cast<SmsMessage*>(smsMsg.get())->GetData();
    return true;
  }

  NS_WARNING("Cannot get MobileMessageData");
  return false;
}

bool
SmsParent::RecvHasSupport(bool* aHasSupport)
{
  *aHasSupport = false;

  nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsService, true);

  smsService->HasSupport(aHasSupport);
  return true;
}

bool
SmsParent::RecvGetSegmentInfoForText(const nsString& aText,
                                     SmsSegmentInfoData* aResult)
{
  aResult->segments() = 0;
  aResult->charsPerSegment() = 0;
  aResult->charsAvailableInLastSegment() = 0;

  nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(smsService, true);

  nsCOMPtr<nsIDOMMozSmsSegmentInfo> info;
  nsresult rv = smsService->GetSegmentInfoForText(aText, getter_AddRefs(info));
  NS_ENSURE_SUCCESS(rv, true);

  int segments, charsPerSegment, charsAvailableInLastSegment;
  if (NS_FAILED(info->GetSegments(&segments)) ||
      NS_FAILED(info->GetCharsPerSegment(&charsPerSegment)) ||
      NS_FAILED(info->GetCharsAvailableInLastSegment(&charsAvailableInLastSegment))) {
    NS_ERROR("Can't get attribute values from nsIDOMMozSmsSegmentInfo");
    return true;
  }

  aResult->segments() = segments;
  aResult->charsPerSegment() = charsPerSegment;
  aResult->charsAvailableInLastSegment() = charsAvailableInLastSegment;
  return true;
}

bool
SmsParent::RecvClearMessageList(const int32_t& aListId)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(mobileMessageDBService, true);

  mobileMessageDBService->ClearMessageList(aListId);
  return true;
}

bool
SmsParent::RecvPSmsRequestConstructor(PSmsRequestParent* aActor,
                                      const IPCSmsRequest& aRequest)
{
  SmsRequestParent* actor = static_cast<SmsRequestParent*>(aActor);

  switch (aRequest.type()) {
    case IPCSmsRequest::TCreateMessageListRequest:
      return actor->DoRequest(aRequest.get_CreateMessageListRequest());
    case IPCSmsRequest::TSendMessageRequest:
      return actor->DoRequest(aRequest.get_SendMessageRequest());
    case IPCSmsRequest::TGetMessageRequest:
      return actor->DoRequest(aRequest.get_GetMessageRequest());
    case IPCSmsRequest::TDeleteMessageRequest:
      return actor->DoRequest(aRequest.get_DeleteMessageRequest());
    case IPCSmsRequest::TGetNextMessageInListRequest:
      return actor->DoRequest(aRequest.get_GetNextMessageInListRequest());
    case IPCSmsRequest::TMarkMessageReadRequest:
      return actor->DoRequest(aRequest.get_MarkMessageReadRequest());
    case IPCSmsRequest::TGetThreadListRequest:
      return actor->DoRequest(aRequest.get_GetThreadListRequest());
    default:
      MOZ_NOT_REACHED("Unknown type!");
      return false;
  }

  MOZ_NOT_REACHED("Should never get here!");
  return false;
}

PSmsRequestParent*
SmsParent::AllocPSmsRequest(const IPCSmsRequest& aRequest)
{
  return new SmsRequestParent();
}

bool
SmsParent::DeallocPSmsRequest(PSmsRequestParent* aActor)
{
  delete aActor;
  return true;
}

/*******************************************************************************
 * SmsRequestParent
 ******************************************************************************/
SmsRequestParent::SmsRequestParent()
{
  MOZ_COUNT_CTOR(SmsRequestParent);
}

SmsRequestParent::~SmsRequestParent()
{
  MOZ_COUNT_DTOR(SmsRequestParent);
}

void
SmsRequestParent::ActorDestroy(ActorDestroyReason aWhy)
{
  if (mSmsRequest) {
    mSmsRequest->SetActorDied();
    mSmsRequest = nullptr;
  }
  if (mCallback) {
    mCallback->SetActorDied();
    mCallback = nullptr;
  }
}

void
SmsRequestParent::SendReply(const MessageReply& aReply) {
  if (!Send__delete__(this, aReply)) {
    NS_WARNING("Failed to send response to child process!");
  }
}

bool
SmsRequestParent::DoRequest(const SendMessageRequest& aRequest)
{
  switch(aRequest.type()) {
  case SendMessageRequest::TSendSmsMessageRequest:
    {
      nsCOMPtr<nsISmsService> smsService = do_GetService(SMS_SERVICE_CONTRACTID);
      NS_ENSURE_TRUE(smsService, true);

      const SendSmsMessageRequest &data = aRequest.get_SendSmsMessageRequest();

      mSmsRequest = SmsRequest::Create(this);
      nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
      nsresult rv = smsService->Send(data.number(), data.message(), forwarder);
      NS_ENSURE_SUCCESS(rv, false);
    }
    break;
  case SendMessageRequest::TSendMmsMessageRequest:
    {
      nsCOMPtr<nsIMmsService> mmsService = do_GetService(MMS_SERVICE_CONTRACTID);
      NS_ENSURE_TRUE(mmsService, true);

      mCallback = new MobileMessageCallback(this);
      JS::Value params;
      if (!GetParamsFromSendMmsMessageRequest(
              aRequest.get_SendMmsMessageRequest(),
              &params)) {
        NS_WARNING("SmsRequestParent: Fail to build MMS params.");
        return false;
      }
      nsresult rv = mmsService->Send(params, mCallback);
      NS_ENSURE_SUCCESS(rv, false);
    }
    break;
  default:
    MOZ_NOT_REACHED("Unknown type of SendMessageRequest!");
    return false;
  }

  return true;
}

bool
SmsRequestParent::DoRequest(const GetMessageRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(mobileMessageDBService, true);

  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  nsresult rv = mobileMessageDBService->GetMessageMoz(aRequest.messageId(), forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
SmsRequestParent::DoRequest(const DeleteMessageRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(mobileMessageDBService, true);

  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  nsresult rv = mobileMessageDBService->DeleteMessage(aRequest.messageId(), forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
SmsRequestParent::DoRequest(const CreateMessageListRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);

  NS_ENSURE_TRUE(mobileMessageDBService, true);
  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  SmsFilter *filter = new SmsFilter(aRequest.filter());
  nsresult rv = mobileMessageDBService->CreateMessageList(filter, aRequest.reverse(), forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
SmsRequestParent::DoRequest(const GetNextMessageInListRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);

  NS_ENSURE_TRUE(mobileMessageDBService, true);
  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  nsresult rv = mobileMessageDBService->GetNextMessageInList(aRequest.aListId(), forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
SmsRequestParent::DoRequest(const MarkMessageReadRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);

  NS_ENSURE_TRUE(mobileMessageDBService, true);
  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  nsresult rv = mobileMessageDBService->MarkMessageRead(aRequest.messageId(), aRequest.value(), forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

bool
SmsRequestParent::DoRequest(const GetThreadListRequest& aRequest)
{
  nsCOMPtr<nsIMobileMessageDatabaseService> mobileMessageDBService =
    do_GetService(MOBILE_MESSAGE_DATABASE_SERVICE_CONTRACTID);

  NS_ENSURE_TRUE(mobileMessageDBService, true);
  mSmsRequest = SmsRequest::Create(this);
  nsCOMPtr<nsIMobileMessageCallback> forwarder = new SmsRequestForwarder(mSmsRequest);
  nsresult rv = mobileMessageDBService->GetThreadList(forwarder);
  NS_ENSURE_SUCCESS(rv, false);

  return true;
}

} // namespace mobilemessage
} // namespace dom
} // namespace mozilla
