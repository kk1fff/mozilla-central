/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_mobilemessage_MobileMessageCallback_h
#define mozilla_dom_mobilemessage_MobileMessageCallback_h

#include "nsIMobileMessageCallback.h"
#include "mozilla/dom/mobilemessage/PSmsRequest.h"
#include "nsCOMPtr.h"
#include "DOMRequest.h"

class nsIDOMMozMmsMessage;

namespace mozilla {
namespace dom {
namespace mobilemessage {

class SmsRequestParent;
class SendMessageReply;

class MobileMessageCallback MOZ_FINAL : public nsIMobileMessageCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMOBILEMESSAGECALLBACK

  MobileMessageCallback(DOMRequest* aDOMRequest);
  MobileMessageCallback(SmsRequestParent* aReqParent);

  void SetActorDied() {
    mParentAlive = false;
  }

private:
  ~MobileMessageCallback();

  nsRefPtr<DOMRequest> mDOMRequest;
  SmsRequestParent* mParent;
  bool mParentAlive;

  void SendMessageReply(const MessageReply& aReply);
  nsresult NotifySuccess(const jsval& aResult);
  nsresult NotifySuccess(nsISupports *aMessage);
  nsresult NotifyError(int32_t aError);
};

} // namespace mobilemessage
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_mobilemessage_MobileMessageCallback_h
