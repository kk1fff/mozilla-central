/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/TextureParent.h"
#include "mozilla/layers/Compositor.h"
#include "CompositableHost.h"
#include "mozilla/layers/TextureFactoryIdentifier.h" // for TextureInfo
#include "ShadowLayerParent.h"
#include "LayerManagerComposite.h"
#include "Compositor.h"
#include "mozilla/layers/CompositableTransactionParent.h"
namespace mozilla {
namespace layers {

TextureParent::TextureParent(const TextureInfo& aInfo, CompositableParent* aCompositable)
: mTextureInfo(aInfo), mLastSurfaceType(SurfaceDescriptor::Tnull_t)
{
  Compositor* compositor = aCompositable->GetCompositor();
  // I don't know if we really want to enforce that
  // let's see if we ever hit it.
  MOZ_ASSERT(compositor);
  if (compositor) {
    mTextureHost = compositor->CreateTextureHost(aInfo.memoryType,
                                                 aInfo.textureFlags,
                                                 mLastSurfaceType,
                                                 aCompositable->GetCompositableManager());
    aCompositable->GetCompositableHost()->AddTextureHost(mTextureHost);
  }
}

bool
TextureParent::EnsureTextureHost(SurfaceDescriptor::Type aSurfaceType) {
  if (!SurfaceTypeChanged(aSurfaceType)) {
    return false;
  }
  CompositableParent* compParent = static_cast<CompositableParent*>(Manager());
  CompositableHost* compositable = compParent->GetCompositableHost();
  Compositor* compositor = compParent->GetCompositor();
  // I don't know if we really want to enforce that
  // let's see if we ever hit it.
  MOZ_ASSERT(compositor);
  if (compositor) {
    mTextureHost = compositor->CreateTextureHost(mTextureInfo.compositableType,
                                                 mTextureInfo.textureFlags,
                                                 mLastSurfaceType,
                                                 nullptr);
    SetCurrentSurfaceType(aSurfaceType);
    compositable->AddTextureHost(mTextureHost);
    return true;
  }
  return false;
}

TextureParent::~TextureParent()
{
  mTextureHost = nullptr;
}

void TextureParent::SetTextureHost(TextureHost* aHost)
{
  mTextureHost = aHost;
}

CompositableHost* TextureParent::GetCompositableHost() const
{
  CompositableParent* actor
    = static_cast<CompositableParent*>(Manager());
  return actor->GetCompositableHost();
}

TextureHost* TextureParent::GetTextureHost() const
{
  return mTextureHost;
}

bool TextureParent::SurfaceTypeChanged(SurfaceDescriptor::Type aNewSurfaceType)
{
  return mLastSurfaceType != aNewSurfaceType;
}

void TextureParent::SetCurrentSurfaceType(SurfaceDescriptor::Type aNewSurfaceType)
{
  mLastSurfaceType = aNewSurfaceType;
}


} // namespace
} // namespace
