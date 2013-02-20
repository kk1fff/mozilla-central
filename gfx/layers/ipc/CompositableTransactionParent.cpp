/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=8 et :
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositableTransactionParent.h"
#include "ShadowLayers.h"
#include "RenderTrace.h"
#include "ShadowLayersManager.h"
#include "CompositableHost.h"
#include "ShadowLayerParent.h"
#include "TiledLayerBuffer.h"
#include "mozilla/layers/TextureParent.h"
#include "LayerManagerComposite.h"

namespace mozilla {
namespace layers {

//--------------------------------------------------
// Convenience accessors
static ShadowLayerParent*
cast(const PLayerParent* in)
{
  return const_cast<ShadowLayerParent*>(
    static_cast<const ShadowLayerParent*>(in));
}

template<class OpPaintT>
static TextureHost*
AsTextureHost(const OpPaintT& op)
{
  return static_cast<TextureParent*>(op.textureParent())->GetTextureHost();
}

// TODO[nical] we should not need this
template<class OpPaintT>
Layer* GetLayerFromOpPaint(const OpPaintT& op)
{
  PTextureParent* textureParent = op.textureParent();
  CompositableHost* compoHost
    = static_cast<CompositableParent*>(textureParent->Manager())->GetCompositableHost();
  return compoHost ? compoHost->GetLayer() : nullptr;
}

template<class OpCreateT>
static ShadowLayerParent*
AsShadowLayer(const OpCreateT& op)
{
  return cast(op.layerParent());
}

static ShadowLayerParent*
AsShadowLayer(const OpSetRoot& op)
{
  return cast(op.rootParent());
}

bool
CompositableParentManager::ReceiveCompositableUpdate(const CompositableOperation& aEdit,
                                                     const bool& isFirstPaint,
                                                     EditReplyVector& replyv)
{
  switch (aEdit.type()) {
    case CompositableOperation::TOpPaintThebesBuffer: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintThebesBuffer& op = aEdit.get_OpPaintThebesBuffer();
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(GetLayerFromOpPaint(op)->AsShadowLayer());
      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());

      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->Swap(newFront, op.updatedRegion(),
                   &newBack, &newValidRegion,
                   &readonlyFront, &frontUpdatedRegion);

      // We have to invalidate the pixels painted into the new buffer.
      // They might overlap with our old pixels.
      //aNewValidRegionFront->Sub(needsReset ? nsIntRegion() : aOldValidRegionFront, aUpdated);

      replyv.push_back(
        OpThebesBufferSwap(
          op.textureParent(), NULL,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case CompositableOperation::TOpPaintCanvas: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint CanvasLayer"));

      const OpPaintCanvas& op = aEdit.get_OpPaintCanvas();
      ShadowCanvasLayer* canvas = static_cast<ShadowCanvasLayer*>(GetLayerFromOpPaint(op)->AsShadowLayer());

      RenderTraceInvalidateStart(canvas, "FF00FF", canvas->GetVisibleRegion().GetBounds());

      canvas->SetAllocator(this);
      TextureHost* textureHost = AsTextureHost(op);

      SurfaceDescriptor newBack;
      bool success;
      textureHost->Update(op.newFrontBuffer(), &newBack, &success);
      if (!success) {
        NS_ASSERTION(newBack.type() == SurfaceDescriptor::Tnull_t, "fail should give null result");
      }

      canvas->Updated();
      replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));

      RenderTraceInvalidateEnd(canvas, "FF00FF");
      break;
    }
    case CompositableOperation::TOpPaintTexture: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint Texture X"));
      const OpPaintTexture& op = aEdit.get_OpPaintTexture();

      Compositor* compositor = nullptr;
      Layer* layer = GetLayerFromOpPaint(op);
      ShadowLayer* shadowLayer = layer ? layer->AsShadowLayer() : nullptr;
      if (shadowLayer) {
         compositor = static_cast<LayerManagerComposite*>(layer->Manager())->GetCompositor();
      } else {
        NS_WARNING("Trying to paint before OpAttachAsyncTexture?");
      }

      TextureParent* textureParent = static_cast<TextureParent*>(op.textureParent());
      const SurfaceDescriptor& descriptor = op.image();
      const TextureInfo& info = textureParent->GetTextureInfo();
      if (compositor) {
        textureParent->GetCompositableHost()->SetCompositor(compositor);
      }
      textureParent->EnsureTextureHost(descriptor.type());
      
      if (!textureParent->GetTextureHost()) {
        NS_WARNING("failed to create the texture host");
        break;
      }

      TextureHost* host = AsTextureHost(op);
      shadowLayer->SetAllocator(this);
      SurfaceDescriptor newBack;
      RenderTraceInvalidateStart(layer, "FF00FF", layer->GetVisibleRegion().GetBounds());
      host->Update(op.image(), &newBack);
      replyv.push_back(OpTextureSwap(op.textureParent(), nullptr, newBack));

      RenderTraceInvalidateEnd(layer, "FF00FF");
      break;
    }
    case CompositableOperation::TOpPaintTextureRegion: {
      MOZ_LAYERS_LOG(("[ParentSide] Paint ThebesLayer"));

      const OpPaintTextureRegion& op = aEdit.get_OpPaintTextureRegion();
      ShadowThebesLayer* thebes =
        static_cast<ShadowThebesLayer*>(GetLayerFromOpPaint(op));
      const ThebesBuffer& newFront = op.newFrontBuffer();

      RenderTraceInvalidateStart(thebes, "FF00FF", op.updatedRegion().GetBounds());

      thebes->SetAllocator(this);
      OptionalThebesBuffer newBack;
      nsIntRegion newValidRegion;
      OptionalThebesBuffer readonlyFront;
      nsIntRegion frontUpdatedRegion;
      thebes->SwapTexture(newFront, op.updatedRegion(),
                          &newBack, &newValidRegion,
                          &readonlyFront, &frontUpdatedRegion);
      replyv.push_back(
        OpThebesBufferSwap(
          op.textureParent(), nullptr,
          newBack, newValidRegion,
          readonlyFront, frontUpdatedRegion));

      RenderTraceInvalidateEnd(thebes, "FF00FF");
      break;
    }
    case CompositableOperation::TOpUpdatePictureRect: {
      const OpUpdatePictureRect& op = aEdit.get_OpUpdatePictureRect();
      CompositableHost* compositable
       = static_cast<CompositableParent*>(op.compositableParent())->GetCompositableHost();
      MOZ_ASSERT(compositable);
      compositable->SetPictureRect(op.picture());
      break;
    }
  }

  return true;
}

} // namespace
} // namespace

