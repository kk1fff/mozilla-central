/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/ContentChild.h"
#include "SmsIPCService.h"
#include "nsXULAppAPI.h"
#include "jsapi.h"
#include "mozilla/dom/mobilemessage/SmsChild.h"
#include "SmsMessage.h"
#include "SmsFilter.h"
#include "SmsRequest.h"
#include "SmsSegmentInfo.h"
#include "DictionaryHelpers.h"
#include "nsJSUtils.h"
#include "nsContentUtils.h"

namespace mozilla {
namespace dom {
namespace mobilemessage {

PSmsChild* gSmsChild;

NS_IMPL_ISUPPORTS3(SmsIPCService,
                   nsISmsService,
                   nsIMmsService,
                   nsIMobileMessageDatabaseService)

void
SendRequest(const IPCSmsRequest& aRequest, nsIMobileMessageCallback* aRequestReply)
{
  MOZ_ASSERT(NS_IsMainThread());

  NS_WARN_IF_FALSE(gSmsChild,
                   "Calling methods on SmsIPCService during "
                   "shutdown!");

  if (gSmsChild) {
    SmsRequestChild* actor = new SmsRequestChild(aRequestReply);
    gSmsChild->SendPSmsRequestConstructor(actor, aRequest);
  }
}

PSmsChild*
SmsIPCService::GetSmsChild()
{
  if (!gSmsChild) {
    gSmsChild = ContentChild::GetSingleton()->SendPSmsConstructor();
  }

  return gSmsChild;
}

SmsIPCService::SmsIPCService() {
  GetSmsChild();
}

/*
 * Implementation of nsISmsService.
 */
NS_IMETHODIMP
SmsIPCService::HasSupport(bool* aHasSupport)
{
  GetSmsChild()->SendHasSupport(aHasSupport);

  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetSegmentInfoForText(const nsAString & aText,
                                     nsIDOMMozSmsSegmentInfo** aResult)
{
  SmsSegmentInfoData data;
  bool ok = GetSmsChild()->SendGetSegmentInfoForText(nsString(aText), &data);
  NS_ENSURE_TRUE(ok, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDOMMozSmsSegmentInfo> info = new SmsSegmentInfo(data);
  info.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::Send(const nsAString& aNumber,
                    const nsAString& aMessage,
                    nsIMobileMessageCallback* aRequest)
{
  SendRequest(SendMessageRequest(SendSmsMessageRequest(nsString(aNumber), nsString(aMessage))),
              aRequest);
  return NS_OK;
}

/*
 * Implementation of nsIMobileMessageDatabaseService.
 */
NS_IMETHODIMP
SmsIPCService::GetMessageMoz(int32_t aMessageId,
                             nsIMobileMessageCallback* aRequest)
{
  SendRequest(GetMessageRequest(aMessageId), aRequest);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::DeleteMessage(int32_t aMessageId,
                             nsIMobileMessageCallback* aRequest)
{
  SendRequest(DeleteMessageRequest(aMessageId), aRequest);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::CreateMessageList(nsIDOMMozSmsFilter* aFilter,
                                 bool aReverse,
                                 nsIMobileMessageCallback* aRequest)
{
  SmsFilterData data = SmsFilterData(static_cast<SmsFilter*>(aFilter)->GetData());
  SendRequest(CreateMessageListRequest(data, aReverse), aRequest);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetNextMessageInList(int32_t aListId,
                                    nsIMobileMessageCallback* aRequest)
{
  SendRequest(GetNextMessageInListRequest(aListId), aRequest);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::ClearMessageList(int32_t aListId)
{
  GetSmsChild()->SendClearMessageList(aListId);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::MarkMessageRead(int32_t aMessageId,
                               bool aValue,
                               nsIMobileMessageCallback* aRequest)
{
  SendRequest(MarkMessageReadRequest(aMessageId, aValue), aRequest);
  return NS_OK;
}

NS_IMETHODIMP
SmsIPCService::GetThreadList(nsIMobileMessageCallback* aRequest)
{
  SendRequest(GetThreadListRequest(), aRequest);
  return NS_OK;
}

bool
GetSendMmsMessageRequestFromParams(const JS::Value& aParam,
                                   SendMmsMessageRequest& request) {
  if (aParam.isUndefined() || aParam.isNull() || !aParam.isObject()) {
    return false;
  }

  AutoJSContext cx;
  mozilla::idl::MmsParameters params;
  nsresult rv = params.Init(cx, &aParam);
  NS_ENSURE_SUCCESS(rv, false);

  uint32_t len;

  // SendMobileMessageRequest.receivers
  JSObject &receiversObj = params.receivers.toObject();
  MOZ_ALWAYS_TRUE(JS_GetArrayLength(cx, &receiversObj, &len));
  
  request.receivers().SetCapacity(len);

  for (uint32_t i = 0; i < len; i++) {
    JS::Value val;
    MOZ_ALWAYS_TRUE(JS_GetElement(cx, &receiversObj, i, &val));

    nsDependentJSString str;
    MOZ_ALWAYS_TRUE(str.init(cx, val.toString()));

    request.receivers().AppendElement(str);
  }

  // SendMobileMessageRequest.attachments
  ContentChild* cc = ContentChild::GetSingleton();

  if (!params.attachments.isObject()) {
    return false;
  }
  JSObject &attachmentsObj = params.attachments.toObject();
  MOZ_ALWAYS_TRUE(JS_GetArrayLength(cx, &attachmentsObj, &len));
  request.attachments().SetCapacity(len);

  for (uint32_t i = 0; i < len; i++) {
    JS::Value val;
    MOZ_ALWAYS_TRUE(JS_GetElement(cx, &attachmentsObj, i, &val));

    mozilla::idl::MmsAttachment attachment;
    rv = attachment.Init(cx, &val);
    NS_ENSURE_SUCCESS(rv, false);

    MmsAttachmentData mmsAttachment;
    mmsAttachment.id().Assign(attachment.id);
    mmsAttachment.location().Assign(attachment.location);
    mmsAttachment.contentChild() = cc->GetOrCreateActorForBlob(attachment.content);

    request.attachments().AppendElement(mmsAttachment);
  }

  request.smil() = params.smil;
  request.subject() = params.subject;

  return true;
}

NS_IMETHODIMP
SmsIPCService::Send(const JS::Value& aParameters,
                    nsIMobileMessageCallback *aRequest)
{
  SendMmsMessageRequest req;
  if (!GetSendMmsMessageRequestFromParams(aParameters, req)) {
    return NS_ERROR_UNEXPECTED;
  };
  SendRequest(SendMessageRequest(req), aRequest);
  return NS_OK;
}

} // namespace mobilemessage
} // namespace dom
} // namespace mozilla
