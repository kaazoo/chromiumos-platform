// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/message_loop/message_pump_type.h>
#include <base/posix/eintr_wrapper.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/system/handle.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/system/platform_handle.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ocr/mojo/ocr_service.mojom.h"
#include "ocr/ocr_service_impl.h"

namespace ocr {

namespace {

namespace mojo_ipc = chromeos::ocr::mojom;

// The relative path of the input test image.
constexpr char kTestImageRelativePath[] = "phototest.tif";
// The name of the output pdf file.
constexpr char kOutputPdfFilename[] = "phototest.pdf";

mojo::ScopedHandle GetInputFileHandle(const std::string& input_filename) {
  base::ScopedFD input_fd(HANDLE_EINTR(
      open(input_filename.c_str(), O_RDONLY | O_NOFOLLOW | O_NOCTTY)));
  return mojo::WrapPlatformHandle(mojo::PlatformHandle(std::move(input_fd)));
}

mojo::ScopedHandle GetOutputFileHandle(const std::string& output_filename) {
  base::ScopedFD out_fd(
      HANDLE_EINTR(open(output_filename.c_str(), O_CREAT | O_WRONLY, 0644)));
  return mojo::WrapPlatformHandle(mojo::PlatformHandle(std::move(out_fd)));
}

}  // namespace

class OcrServiceFuzzer {
 public:
  OcrServiceFuzzer() {
    logging::SetMinLogLevel(logging::LOGGING_FATAL);
    mojo::core::Init();
    ocr_service_impl_.AddReceiver(remote.BindNewPipeAndPassReceiver(),
                                  false /* should_quit */);
  }

 public:
  mojo::Remote<mojo_ipc::OpticalCharacterRecognitionService> remote;

 private:
  OcrServiceImpl ocr_service_impl_;
  base::SingleThreadTaskExecutor executor_{base::MessagePumpType::IO};
};

// Tests OCR on a random input image generated by fuzzing data.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static OcrServiceFuzzer fuzzer_env;

  // Write the fuzzer data to a file.
  base::ScopedTempDir temp_dir_;
  CHECK(temp_dir_.CreateUniqueTempDir());
  base::FilePath file_path = temp_dir_.GetPath().Append(kTestImageRelativePath);
  base::WriteFile(file_path, (const char*)data, size);

  // Construct request.
  const std::string input_image_filename = file_path.value();
  mojo::ScopedHandle input_fd_handle = GetInputFileHandle(input_image_filename);
  const std::string output_filename =
      temp_dir_.GetPath().Append(kOutputPdfFilename).value();
  mojo::ScopedHandle output_fd_handle = GetOutputFileHandle(output_filename);
  mojo_ipc::OcrConfigPtr ocr_config = mojo_ipc::OcrConfig::New();
  mojo_ipc::PdfRendererConfigPtr pdf_renderer_config =
      mojo_ipc::PdfRendererConfig::New();

  // Perform OCR.
  bool ocr_callback_done = false;
  fuzzer_env.remote->GenerateSearchablePdfFromImage(
      std::move(input_fd_handle), std::move(output_fd_handle),
      std::move(ocr_config), std::move(pdf_renderer_config),
      base::BindOnce(
          [](bool* ocr_callback_done,
             const mojo_ipc::OpticalCharacterRecognitionServiceResponsePtr
                 response) { *ocr_callback_done = true; },
          &ocr_callback_done));
  base::RunLoop().RunUntilIdle();
  return 0;
}

}  // namespace ocr
