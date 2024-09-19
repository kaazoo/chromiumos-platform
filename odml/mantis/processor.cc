// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <fstream>
#include <vector>

#include <base/check.h>

#include "odml/mantis/lib_api.h"
#include "odml/mojom/mantis_processor.mojom.h"

namespace mantis {

namespace {
using mojom::MantisError;
using mojom::MantisResult;
}  // namespace

MantisProcessor::MantisProcessor(
    MantisComponent component,
    const MantisAPI* api,
    mojo::PendingReceiver<mojom::MantisProcessor> receiver)
    : component_(component), api_(api) {
  CHECK(api_);
  if (!component_.processor) {
    LOG(ERROR) << "Processor is missing";
  }
  receiver_set_.Add(this, std::move(receiver));
}

MantisProcessor::~MantisProcessor() {
  api_->DestroyMantisComponent(component_);
}

void MantisProcessor::Inpainting(const std::vector<uint8_t>& image,
                                 const std::vector<uint8_t>& mask,
                                 uint32_t seed,
                                 InpaintingCallback callback) {
  auto result = MantisResult::NewError(MantisError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

void MantisProcessor::GenerativeFill(const std::vector<uint8_t>& image,
                                     const std::vector<uint8_t>& mask,
                                     uint32_t seed,
                                     const std::string& prompt,
                                     GenerativeFillCallback callback) {
  auto result = MantisResult::NewError(MantisError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

void MantisProcessor::Segmentation(const std::vector<uint8_t>& image,
                                   const std::vector<uint8_t>& prior,
                                   SegmentationCallback callback) {
  auto result = MantisResult::NewError(MantisError::kUnknownError);
  std::move(callback).Run(std::move(result));
}

}  // namespace mantis
