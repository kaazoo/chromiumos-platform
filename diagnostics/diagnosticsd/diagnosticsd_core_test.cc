// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/memory/shared_memory.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <dbus/diagnosticsd/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/property.h>
#include <gmock/gmock.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_ptr.h>

#include "diagnostics/diagnosticsd/diagnosticsd_core.h"
#include "diagnostics/diagnosticsd/ec_constants.h"
#include "diagnostics/diagnosticsd/fake_browser.h"
#include "diagnostics/diagnosticsd/fake_diagnostics_processor.h"
#include "diagnostics/diagnosticsd/file_test_utils.h"
#include "diagnostics/diagnosticsd/mojo_test_utils.h"
#include "diagnostics/diagnosticsd/mojo_utils.h"
#include "diagnostics/diagnosticsd/protobuf_test_utils.h"
#include "diagnosticsd.pb.h"  // NOLINT(build/include)
#include "mojo/diagnosticsd.mojom.h"

using testing::_;
using testing::Invoke;
using testing::Mock;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;
using testing::WithArg;

namespace diagnostics {

// Templates for the gRPC URIs that should be used for testing. "%s" is
// substituted with a temporary directory.
const char kDiagnosticsdGrpcUriTemplate[] = "unix:%s/test_diagnosticsd_socket";
const char kDiagnosticsProcessorGrpcUriTemplate[] =
    "unix:%s/test_diagnostics_processor_socket";
const char kUiMessageReceiverDiagnosticsProcessorGrpcUriTemplate[] =
    "unix:%s/test_ui_message_receiver_diagnostics_processor_socket";

using MojomDiagnosticsdService =
    chromeos::diagnosticsd::mojom::DiagnosticsdService;
using MojomDiagnosticsdServiceFactory =
    chromeos::diagnosticsd::mojom::DiagnosticsdServiceFactory;

namespace {

// Returns a callback that, once called, saves its parameter to |*response| and
// quits |*run_loop|.
template <typename ValueType>
base::Callback<void(std::unique_ptr<ValueType>)> MakeAsyncResponseWriter(
    std::unique_ptr<ValueType>* response, base::RunLoop* run_loop) {
  return base::Bind(
      [](std::unique_ptr<ValueType>* response, base::RunLoop* run_loop,
         std::unique_ptr<ValueType> received_response) {
        EXPECT_TRUE(received_response);
        EXPECT_FALSE(*response);
        *response = std::move(received_response);
        run_loop->Quit();
      },
      base::Unretained(response), base::Unretained(run_loop));
}

class MockDiagnosticsdCoreDelegate : public DiagnosticsdCore::Delegate {
 public:
  std::unique_ptr<mojo::Binding<MojomDiagnosticsdServiceFactory>>
  BindDiagnosticsdMojoServiceFactory(
      MojomDiagnosticsdServiceFactory* mojo_service_factory,
      base::ScopedFD mojo_pipe_fd) override {
    // Redirect to a separate mockable method to workaround GMock's issues with
    // move-only types.
    return std::unique_ptr<mojo::Binding<MojomDiagnosticsdServiceFactory>>(
        BindDiagnosticsdMojoServiceFactoryImpl(mojo_service_factory,
                                               mojo_pipe_fd.get()));
  }

  MOCK_METHOD2(BindDiagnosticsdMojoServiceFactoryImpl,
               mojo::Binding<MojomDiagnosticsdServiceFactory>*(
                   MojomDiagnosticsdServiceFactory* mojo_service_factory,
                   int mojo_pipe_fd));
  MOCK_METHOD0(BeginDaemonShutdown, void());
};

// Tests for the DiagnosticsdCore class.
class DiagnosticsdCoreTest : public testing::Test {
 protected:
  DiagnosticsdCoreTest() { InitializeMojo(); }

  ~DiagnosticsdCoreTest() { SetDBusShutdownExpectations(); }

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    diagnosticsd_grpc_uri_ = base::StringPrintf(
        kDiagnosticsdGrpcUriTemplate, temp_dir_.GetPath().value().c_str());
    ui_message_receiver_diagnostics_processor_grpc_uri_ = base::StringPrintf(
        kUiMessageReceiverDiagnosticsProcessorGrpcUriTemplate,
        temp_dir_.GetPath().value().c_str());

    diagnostics_processor_grpc_uri_ =
        base::StringPrintf(kDiagnosticsProcessorGrpcUriTemplate,
                           temp_dir_.GetPath().value().c_str());

    core_ = std::make_unique<DiagnosticsdCore>(
        diagnosticsd_grpc_uri_,
        ui_message_receiver_diagnostics_processor_grpc_uri_,
        std::vector<std::string>{diagnostics_processor_grpc_uri_},
        &core_delegate_);
    core_->set_root_dir_for_testing(temp_dir_.GetPath());

    SetUpEcEventService();

    ASSERT_TRUE(core_->Start());

    SetUpEcEventServiceFifoWriteEnd();

    SetUpDBus();

    fake_browser_ =
        std::make_unique<FakeBrowser>(&mojo_service_factory_interface_ptr_,
                                      bootstrap_mojo_connection_dbus_method_);
  }

  void TearDown() override {
    base::RunLoop run_loop;
    core_->ShutDown(run_loop.QuitClosure());
    run_loop.Run();
  }

  const base::FilePath& temp_dir_path() const {
    DCHECK(temp_dir_.IsValid());
    return temp_dir_.GetPath();
  }

  MockDiagnosticsdCoreDelegate* core_delegate() { return &core_delegate_; }

  mojo::InterfacePtr<MojomDiagnosticsdServiceFactory>*
  mojo_service_factory_interface_ptr() {
    return &mojo_service_factory_interface_ptr_;
  }

  FakeBrowser* fake_browser() {
    DCHECK(fake_browser_);
    return fake_browser_.get();
  }

  // Set up mock for BindDiagnosticsdMojoServiceFactory() that simulates
  // successful Mojo service binding to the given file descriptor. After the
  // mock gets triggered, |mojo_service_factory_interface_ptr_| become
  // initialized to point to the tested Mojo service.
  void SetSuccessMockBindDiagnosticsdMojoService(
      FakeMojoFdGenerator* fake_mojo_fd_generator) {
    EXPECT_CALL(core_delegate_, BindDiagnosticsdMojoServiceFactoryImpl(_, _))
        .WillOnce(Invoke([fake_mojo_fd_generator, this](
                             MojomDiagnosticsdServiceFactory*
                                 mojo_service_factory,
                             int mojo_pipe_fd) {
          // Verify the file descriptor is a duplicate of an expected one.
          EXPECT_TRUE(fake_mojo_fd_generator->IsDuplicateFd(mojo_pipe_fd));
          // Initialize a Mojo binding that, instead of working through the
          // given (fake) file descriptor, talks to the test endpoint
          // |mojo_service_interface_ptr_|.
          auto mojo_service_factory_binding =
              std::make_unique<mojo::Binding<MojomDiagnosticsdServiceFactory>>(
                  mojo_service_factory, &mojo_service_factory_interface_ptr_);
          DCHECK(mojo_service_factory_interface_ptr_);
          return mojo_service_factory_binding.release();
        }));
  }

  void WriteEcEventToSysfsFile(
      const DiagnosticsdEcEventService::EcEvent& ec_event) const {
    ASSERT_EQ(write(ec_event_service_fd_.get(), &ec_event, sizeof(ec_event)),
              sizeof(ec_event));
  }

  dbus::ExportedObject::MethodCallCallback
  bootstrap_mojo_connection_dbus_method() {
    return bootstrap_mojo_connection_dbus_method_;
  }

  const std::string& diagnosticsd_grpc_uri() const {
    DCHECK(!diagnosticsd_grpc_uri_.empty());
    return diagnosticsd_grpc_uri_;
  }

  const std::string& ui_message_receiver_diagnostics_processor_grpc_uri()
      const {
    DCHECK(!ui_message_receiver_diagnostics_processor_grpc_uri_.empty());
    return ui_message_receiver_diagnostics_processor_grpc_uri_;
  }

  const std::string& diagnostics_processor_grpc_uri() const {
    DCHECK(!diagnostics_processor_grpc_uri_.empty());
    return diagnostics_processor_grpc_uri_;
  }

 private:
  // Initialize the Mojo subsystem.
  void InitializeMojo() { mojo::edk::Init(); }

  // Perform initialization of the D-Bus object exposed by the tested code.
  void SetUpDBus() {
    const dbus::ObjectPath kDBusObjectPath(kDiagnosticsdServicePath);

    // Expect that the /org/chromium/Diagnosticsd object is exported.
    diagnosticsd_dbus_object_ = new StrictMock<dbus::MockExportedObject>(
        dbus_bus_.get(), kDBusObjectPath);
    EXPECT_CALL(*dbus_bus_, GetExportedObject(kDBusObjectPath))
        .WillOnce(Return(diagnosticsd_dbus_object_.get()));

    // Expect that standard methods on the org.freedesktop.DBus.Properties
    // interface are exported.
    EXPECT_CALL(
        *diagnosticsd_dbus_object_,
        ExportMethod(dbus::kPropertiesInterface, dbus::kPropertiesGet, _, _));
    EXPECT_CALL(
        *diagnosticsd_dbus_object_,
        ExportMethod(dbus::kPropertiesInterface, dbus::kPropertiesSet, _, _));
    EXPECT_CALL(*diagnosticsd_dbus_object_,
                ExportMethod(dbus::kPropertiesInterface,
                             dbus::kPropertiesGetAll, _, _));

    // Expect that methods on the org.chromium.DiagnosticsdInterface interface
    // are exported.
    EXPECT_CALL(*diagnosticsd_dbus_object_,
                ExportMethod(kDiagnosticsdServiceInterface,
                             kDiagnosticsdBootstrapMojoConnectionMethod, _, _))
        .WillOnce(SaveArg<2 /* method_call_callback */>(
            &bootstrap_mojo_connection_dbus_method_));

    // Run the tested code that exports D-Bus objects and methods.
    scoped_refptr<brillo::dbus_utils::AsyncEventSequencer> dbus_sequencer(
        new brillo::dbus_utils::AsyncEventSequencer());
    core_->RegisterDBusObjectsAsync(dbus_bus_, dbus_sequencer.get());

    // Verify that required D-Bus methods are exported.
    EXPECT_FALSE(bootstrap_mojo_connection_dbus_method_.is_null());
  }

  // Set mock expectations for calls triggered during test destruction.
  void SetDBusShutdownExpectations() {
    EXPECT_CALL(*diagnosticsd_dbus_object_, Unregister());
  }

  // Creates FIFO to emulates the EC event service sysfs event file.
  void SetUpEcEventService() {
    core_->set_ec_event_service_fd_events_for_testing(POLLIN);
    ASSERT_TRUE(base::CreateDirectory(ec_event_sysfs_file_path().DirName()));
    ASSERT_EQ(mkfifo(ec_event_sysfs_file_path().value().c_str(), 0600), 0);
  }

  // Setups |ec_event_service_fd_| FIFO file descriptor. Must be called only
  // after |DiagnosticsdCore::Start()| call. Otherwise, it will block thread.
  void SetUpEcEventServiceFifoWriteEnd() {
    ASSERT_FALSE(ec_event_service_fd_.is_valid());
    ec_event_service_fd_.reset(
        open(ec_event_sysfs_file_path().value().c_str(), O_WRONLY));
    ASSERT_TRUE(ec_event_service_fd_.is_valid());
  }

  base::FilePath ec_event_sysfs_file_path() const {
    return temp_dir_.GetPath().Append(kEcEventSysfsPath);
  }

  base::MessageLoop message_loop_;

  base::ScopedTempDir temp_dir_;

  // gRPC URI on which the tested "Diagnosticsd" gRPC service (owned by
  // DiagnosticsdCore) is listening.
  std::string diagnosticsd_grpc_uri_;
  // gRPC URI on which the fake "DiagnosticsProcessor" gRPC service (owned by
  // FakeDiagnosticsProcessor) is listening, eligible to receive UI messages.
  std::string ui_message_receiver_diagnostics_processor_grpc_uri_;
  // gRPC URI on which the fake "DiagnosticsProcessor" gRPC service (owned by
  // FakeDiagnosticsProcessor) is listening.
  std::string diagnostics_processor_grpc_uri_;

  scoped_refptr<StrictMock<dbus::MockBus>> dbus_bus_ =
      new StrictMock<dbus::MockBus>(dbus::Bus::Options());

  // Mock D-Bus integration helper for the object exposed by the tested code.
  scoped_refptr<StrictMock<dbus::MockExportedObject>> diagnosticsd_dbus_object_;

  // Mojo interface to the service factory exposed by the tested code.
  mojo::InterfacePtr<MojomDiagnosticsdServiceFactory>
      mojo_service_factory_interface_ptr_;

  // Write end of FIFO that emulates EC sysfs event file. EC event service
  // operates with read end of FIFO as with usual file.
  // Must be initialized only after |DiagnosticsdCore::Start()| call.
  base::ScopedFD ec_event_service_fd_;

  StrictMock<MockDiagnosticsdCoreDelegate> core_delegate_;

  std::unique_ptr<DiagnosticsdCore> core_;

  // Callback that the tested code exposed as the BootstrapMojoConnection D-Bus
  // method.
  dbus::ExportedObject::MethodCallCallback
      bootstrap_mojo_connection_dbus_method_;

  std::unique_ptr<FakeBrowser> fake_browser_;
};

}  // namespace

// Test that the Mojo service gets successfully bootstrapped after the
// BootstrapMojoConnection D-Bus method is called.
TEST_F(DiagnosticsdCoreTest, MojoBootstrapSuccess) {
  FakeMojoFdGenerator fake_mojo_fd_generator;
  SetSuccessMockBindDiagnosticsdMojoService(&fake_mojo_fd_generator);

  EXPECT_TRUE(fake_browser()->BootstrapMojoConnection(&fake_mojo_fd_generator));

  EXPECT_TRUE(*mojo_service_factory_interface_ptr());
}

// Test failure to bootstrap the Mojo service due to en error returned by
// BindDiagnosticsdMojoService() delegate method.
TEST_F(DiagnosticsdCoreTest, MojoBootstrapErrorToBind) {
  FakeMojoFdGenerator fake_mojo_fd_generator;
  EXPECT_CALL(*core_delegate(), BindDiagnosticsdMojoServiceFactoryImpl(_, _))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*core_delegate(), BeginDaemonShutdown());

  EXPECT_FALSE(
      fake_browser()->BootstrapMojoConnection(&fake_mojo_fd_generator));

  Mock::VerifyAndClearExpectations(core_delegate());
}

// Test that second attempt to bootstrap the Mojo service results in error and
// the daemon shutdown.
TEST_F(DiagnosticsdCoreTest, MojoBootstrapErrorRepeated) {
  FakeMojoFdGenerator first_fake_mojo_fd_generator;
  SetSuccessMockBindDiagnosticsdMojoService(&first_fake_mojo_fd_generator);

  EXPECT_TRUE(
      fake_browser()->BootstrapMojoConnection(&first_fake_mojo_fd_generator));
  Mock::VerifyAndClearExpectations(core_delegate());

  FakeMojoFdGenerator second_fake_mojo_fd_generator;
  EXPECT_CALL(*core_delegate(), BeginDaemonShutdown());

  EXPECT_FALSE(
      fake_browser()->BootstrapMojoConnection(&second_fake_mojo_fd_generator));

  Mock::VerifyAndClearExpectations(core_delegate());
}

// Test that the daemon gets shut down when the previously bootstrapped Mojo
// connection aborts.
TEST_F(DiagnosticsdCoreTest, MojoBootstrapSuccessThenAbort) {
  FakeMojoFdGenerator fake_mojo_fd_generator;
  SetSuccessMockBindDiagnosticsdMojoService(&fake_mojo_fd_generator);

  EXPECT_TRUE(fake_browser()->BootstrapMojoConnection(&fake_mojo_fd_generator));

  Mock::VerifyAndClearExpectations(core_delegate());

  EXPECT_CALL(*core_delegate(), BeginDaemonShutdown());

  // Abort the Mojo connection by closing the browser-side endpoint.
  mojo_service_factory_interface_ptr()->reset();
  base::RunLoop().RunUntilIdle();
  Mock::VerifyAndClearExpectations(core_delegate());
}

namespace {

// Tests for the DiagnosticsdCore class with the already established Mojo
// connection to the fake browser and gRPC communication with the fake
// diagnostics_processor.
class BootstrappedDiagnosticsdCoreTest : public DiagnosticsdCoreTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(DiagnosticsdCoreTest::SetUp());

    FakeMojoFdGenerator fake_mojo_fd_generator;
    SetSuccessMockBindDiagnosticsdMojoService(&fake_mojo_fd_generator);
    ASSERT_TRUE(
        fake_browser()->BootstrapMojoConnection(&fake_mojo_fd_generator));
    ASSERT_TRUE(*mojo_service_factory_interface_ptr());

    fake_diagnostics_processor_ = std::make_unique<FakeDiagnosticsProcessor>(
        diagnostics_processor_grpc_uri(), diagnosticsd_grpc_uri());

    fake_ui_message_receiver_diagnostics_processor_ =
        std::make_unique<FakeDiagnosticsProcessor>(
            ui_message_receiver_diagnostics_processor_grpc_uri(),
            diagnosticsd_grpc_uri());
  }

  void TearDown() override {
    fake_diagnostics_processor_.reset();
    fake_ui_message_receiver_diagnostics_processor_.reset();
    DiagnosticsdCoreTest::TearDown();
  }

  FakeDiagnosticsProcessor* fake_ui_message_receiver_diagnostics_processor() {
    return fake_ui_message_receiver_diagnostics_processor_.get();
  }

  FakeDiagnosticsProcessor* fake_diagnostics_processor() {
    return fake_diagnostics_processor_.get();
  }

  base::Callback<void(mojo::ScopedHandle)> fake_browser_valid_handle_callback(
      const base::Closure& callback,
      const std::string& expected_response_json_message) {
    return base::Bind(
        [](const base::Closure& callback,
           const std::string& expected_response_json_message,
           mojo::ScopedHandle response_json_message_handle) {
          std::unique_ptr<base::SharedMemory> shared_memory =
              GetReadOnlySharedMemoryFromMojoHandle(
                  std::move(response_json_message_handle));
          ASSERT_TRUE(shared_memory);
          ASSERT_EQ(
              expected_response_json_message,
              std::string(static_cast<const char*>(shared_memory->memory()),
                          shared_memory->mapped_size()));
          callback.Run();
        },
        callback, expected_response_json_message);
  }

  base::Callback<void(mojo::ScopedHandle)> fake_browser_invalid_handle_callback(
      const base::Closure& callback) {
    return base::Bind(
        [](const base::Closure& callback,
           mojo::ScopedHandle response_json_message_handle) {
          ASSERT_FALSE(response_json_message_handle.is_valid());
          callback.Run();
        },
        callback);
  }

 private:
  std::unique_ptr<FakeDiagnosticsProcessor>
      fake_ui_message_receiver_diagnostics_processor_;
  std::unique_ptr<FakeDiagnosticsProcessor> fake_diagnostics_processor_;
};

}  // namespace

// Test that the UI message receiver diagnostics processor will receive message
// from browser.
TEST_F(BootstrappedDiagnosticsdCoreTest,
       SendGrpcUiMessageToDiagnosticsProcessor) {
  const std::string json_message = "{\"some_key\": \"some_value\"}";
  const std::string response_json_message = "{\"key\": \"value\"}";

  base::RunLoop run_loop_diagnostics_processor;
  base::RunLoop run_loop_fake_browser;

  fake_ui_message_receiver_diagnostics_processor()
      ->set_handle_message_from_ui_callback(
          run_loop_diagnostics_processor.QuitClosure());
  fake_ui_message_receiver_diagnostics_processor()
      ->set_handle_message_from_ui_json_message_response(response_json_message);
  fake_diagnostics_processor()->set_handle_message_from_ui_callback(
      base::Bind([]() {
        // The diagnostics processor not eligible to receive messages from UI
        // must not receive them.
        FAIL();
      }));

  auto callback = fake_browser_valid_handle_callback(
      run_loop_fake_browser.QuitClosure(), response_json_message);
  EXPECT_TRUE(fake_browser()->SendUiMessageToDiagnosticsProcessor(json_message,
                                                                  callback));

  run_loop_diagnostics_processor.Run();
  run_loop_fake_browser.Run();
  EXPECT_EQ(json_message, fake_ui_message_receiver_diagnostics_processor()
                              ->handle_message_from_ui_actual_json_message());
}

// Test that the UI message receiver diagnostics processor will not receive
// message from browser if JSON message is invalid.
TEST_F(BootstrappedDiagnosticsdCoreTest,
       SendGrpcUiMessageToDiagnosticsProcessorInvalidJSON) {
  const std::string json_message = "{'some_key': 'some_value'}";

  base::RunLoop run_loop_fake_browser;

  auto callback =
      fake_browser_invalid_handle_callback(run_loop_fake_browser.QuitClosure());
  EXPECT_TRUE(fake_browser()->SendUiMessageToDiagnosticsProcessor(json_message,
                                                                  callback));

  run_loop_fake_browser.Run();
  // There's no reliable way to wait till the wrong HandleMessageFromUi(), if
  // the tested code is buggy and calls it, gets executed. The RunUntilIdle() is
  // used to make the test failing at least with some probability in case of
  // such a bug.
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(fake_ui_message_receiver_diagnostics_processor()
                   ->handle_message_from_ui_actual_json_message()
                   .has_value());
}

// Test that the UI message receiver diagnostics processor will receive message
// from browser.
TEST_F(BootstrappedDiagnosticsdCoreTest,
       SendGrpcUiMessageToDiagnosticsProcessorInvalidResponseJSON) {
  const std::string json_message = "{\"some_key\": \"some_value\"}";
  const std::string response_json_message = "{'key': 'value'}";

  base::RunLoop run_loop_diagnostics_processor;
  base::RunLoop run_loop_fake_browser;

  fake_ui_message_receiver_diagnostics_processor()
      ->set_handle_message_from_ui_callback(
          run_loop_diagnostics_processor.QuitClosure());
  fake_ui_message_receiver_diagnostics_processor()
      ->set_handle_message_from_ui_json_message_response(response_json_message);

  auto callback =
      fake_browser_invalid_handle_callback(run_loop_fake_browser.QuitClosure());
  EXPECT_TRUE(fake_browser()->SendUiMessageToDiagnosticsProcessor(json_message,
                                                                  callback));

  run_loop_diagnostics_processor.Run();
  run_loop_fake_browser.Run();
  EXPECT_EQ(json_message, fake_ui_message_receiver_diagnostics_processor()
                              ->handle_message_from_ui_actual_json_message());
}

// Test that the GetProcData() method exposed by the daemon's gRPC server
// returns a dump of the corresponding file from the disk.
TEST_F(BootstrappedDiagnosticsdCoreTest, GetProcDataGrpcCall) {
  const std::string kFakeFileContents = "foo";
  const base::FilePath file_path = temp_dir_path().Append("proc/uptime");
  ASSERT_TRUE(WriteFileAndCreateParentDirs(file_path, kFakeFileContents));

  grpc_api::GetProcDataRequest request;
  request.set_type(grpc_api::GetProcDataRequest::FILE_UPTIME);
  std::unique_ptr<grpc_api::GetProcDataResponse> response;
  base::RunLoop run_loop;
  fake_diagnostics_processor()->GetProcData(
      request, MakeAsyncResponseWriter(&response, &run_loop));
  run_loop.Run();

  ASSERT_TRUE(response);
  grpc_api::GetProcDataResponse expected_response;
  expected_response.add_file_dump();
  expected_response.mutable_file_dump(0)->set_path(file_path.value());
  expected_response.mutable_file_dump(0)->set_canonical_path(file_path.value());
  expected_response.mutable_file_dump(0)->set_contents(kFakeFileContents);
  EXPECT_TRUE(google::protobuf::util::MessageDifferencer::Equals(
      *response, expected_response))
      << "Obtained: " << response->ShortDebugString()
      << ",\nExpected: " << expected_response.ShortDebugString();
}

// Test that the RunEcCommand() method exposed by the daemon's gRPC server
// writes payload to sysfs file exposed by the EC driver and reads response
// using the same file.
TEST_F(BootstrappedDiagnosticsdCoreTest, RunEcCommandGrpcCall) {
  const base::FilePath file_path =
      temp_dir_path().Append(kEcDriverSysfsPath).Append(kEcRunCommandFilePath);
  const std::string kRequestPayload = "1";
  ASSERT_TRUE(WriteFileAndCreateParentDirs(file_path, ""));

  grpc_api::RunEcCommandRequest request;
  request.set_payload(kRequestPayload);
  std::unique_ptr<grpc_api::RunEcCommandResponse> response;
  base::RunLoop run_loop;
  fake_diagnostics_processor()->RunEcCommand(
      request, MakeAsyncResponseWriter(&response, &run_loop));
  run_loop.Run();

  ASSERT_TRUE(response);
  grpc_api::RunEcCommandResponse expected_response;
  expected_response.set_status(grpc_api::RunEcCommandResponse::STATUS_OK);
  expected_response.set_payload(kRequestPayload);
  EXPECT_THAT(*response, ProtobufEquals(expected_response))
      << "Actual: {" << response->ShortDebugString() << "}";
}

// Test that the GetEcProperty() method exposed by the daemon's gRPC server
// returns a dump of the corresponding file from the disk.
TEST_F(BootstrappedDiagnosticsdCoreTest, GetEcPropertyGrpcCall) {
  const base::FilePath file_path = temp_dir_path()
                                       .Append(kEcDriverSysfsPath)
                                       .Append(kEcDriverSysfsPropertiesPath)
                                       .Append(kEcPropertyGlobalMicMuteLed);
  const std::string kFakeFileContents = "1";
  ASSERT_TRUE(WriteFileAndCreateParentDirs(file_path, kFakeFileContents));

  grpc_api::GetEcPropertyRequest request;
  request.set_property(
      grpc_api::GetEcPropertyRequest::PROPERTY_GLOBAL_MIC_MUTE_LED);
  std::unique_ptr<grpc_api::GetEcPropertyResponse> response;
  base::RunLoop run_loop;
  fake_diagnostics_processor()->GetEcProperty(
      request, MakeAsyncResponseWriter(&response, &run_loop));
  run_loop.Run();

  ASSERT_TRUE(response);
  grpc_api::GetEcPropertyResponse expected_response;
  expected_response.set_status(grpc_api::GetEcPropertyResponse::STATUS_OK);
  expected_response.set_payload(kFakeFileContents);
  EXPECT_THAT(*response, ProtobufEquals(expected_response))
      << "Actual: {" << response->ShortDebugString() << "}";
}

// Test that PerformWebRequest() method exposed by the daemon's gRPC returns a
// Web request response from the browser.
TEST_F(BootstrappedDiagnosticsdCoreTest, PerformWebRequestToBrowser) {
  constexpr char kHttpsUrl[] = "https://www.google.com";
  constexpr int kHttpStatusOk = 200;

  grpc_api::PerformWebRequestParameter request;
  request.set_http_method(
      grpc_api::PerformWebRequestParameter::HTTP_METHOD_GET);
  request.set_url(kHttpsUrl);

  std::unique_ptr<grpc_api::PerformWebRequestResponse> response;
  {
    base::RunLoop run_loop;
    fake_diagnostics_processor()->PerformWebRequest(
        request, MakeAsyncResponseWriter(&response, &run_loop));
    run_loop.Run();
  }

  ASSERT_TRUE(response);
  grpc_api::PerformWebRequestResponse expected_response;
  expected_response.set_status(grpc_api::PerformWebRequestResponse::STATUS_OK);
  expected_response.set_http_status(kHttpStatusOk);
  EXPECT_THAT(*response, ProtobufEquals(expected_response))
      << "Actual: {" << response->ShortDebugString() << "}";
}

namespace {

// Fake types to be used to emulate EC events.
const uint16_t kFakeEcEventType1 = 0xabcd;
const uint16_t kFakeEcEventType2 = 0x1234;

// Tests for EC event service.
class EcEventServiceBootstrappedDiagnosticsdCoreTest
    : public BootstrappedDiagnosticsdCoreTest {
 protected:
  void EmulateEcEvent(uint16_t size, uint16_t type) const {
    WriteEcEventToSysfsFile(GetEcEvent(size, type));
  }

  void ExpectFakeProcessorEcEventCalled(
      FakeDiagnosticsProcessor* fake_diagnostics_processor,
      uint16_t expected_size,
      uint16_t type) {
    const std::string payload = GetPayload(
        expected_size * sizeof(DiagnosticsdEcEventService::EcEvent::data[0]));
    base::RunLoop run_loop;
    fake_diagnostics_processor->set_handle_ec_event_request_callback(
        base::BindRepeating(
            [](const base::Closure& callback, int32_t expected_type,
               const std::string& expected_payload, int32_t type,
               const std::string& payload) {
              ASSERT_EQ(type, expected_type);
              ASSERT_EQ(payload, expected_payload);
              callback.Run();
            },
            run_loop.QuitClosure(), type, payload));
    run_loop.Run();
  }

 private:
  const uint16_t kData[6]{0x0102, 0x1314, 0x2526, 0x3738, 0x494a, 0x5b5c};
  // |kData| bytes little endian representation.
  const uint8_t kPayload[12]{0x02, 0x01, 0x14, 0x13, 0x26, 0x25,
                             0x38, 0x37, 0x4a, 0x49, 0x5c, 0x5b};

  DiagnosticsdEcEventService::EcEvent GetEcEvent(uint16_t size,
                                                 uint16_t type) const {
    return DiagnosticsdEcEventService::EcEvent(size, type, kData);
  }

  std::string GetPayload(size_t expected_size_in_bytes) const {
    return std::string(reinterpret_cast<const char*>(kPayload),
                       expected_size_in_bytes);
  }
};

}  // namespace

// Test that the method |HandleEcNotification()| exposed by diagnostics
// processor gRPC is called by diagnostics deamon.
TEST_F(EcEventServiceBootstrappedDiagnosticsdCoreTest,
       SendGrpcEcEventToDiagnosticsProcessorSize0) {
  EmulateEcEvent(0, kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 0,
                                   kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 0, kFakeEcEventType1);
}

TEST_F(EcEventServiceBootstrappedDiagnosticsdCoreTest,
       SendGrpcEcEventToDiagnosticsProcessorSize5) {
  EmulateEcEvent(5, kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 5,
                                   kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 5, kFakeEcEventType1);
}

TEST_F(EcEventServiceBootstrappedDiagnosticsdCoreTest,
       SendGrpcEcEventToDiagnosticsProcessorSize6) {
  EmulateEcEvent(6, kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 6,
                                   kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 6, kFakeEcEventType1);
}

// Test that the method |HandleEcNotification()| exposed by diagnostics
// processor gRPC is called by diagnostics deamon multiple times.
// TODO(b/124598866): Disabled due to flakiness.
TEST_F(EcEventServiceBootstrappedDiagnosticsdCoreTest,
       DISABLED_SendGrpcEcEventToDiagnosticsProcessorMultipleEvents) {
  EmulateEcEvent(3, kFakeEcEventType1);
  EmulateEcEvent(4, kFakeEcEventType2);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 3,
                                   kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 4,
                                   kFakeEcEventType2);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 3, kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 4, kFakeEcEventType2);
}

// Test that the method |HandleEcNotification()| exposed by diagnostics
// processor gRPC is called by diagnostics deamon even when |ec_event.size|
// exceeds allocated data array.
TEST_F(EcEventServiceBootstrappedDiagnosticsdCoreTest,
       SendGrpcEcEventToDiagnosticsProcessorInvalidSize) {
  EmulateEcEvent(7, kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(fake_diagnostics_processor(), 6,
                                   kFakeEcEventType1);
  ExpectFakeProcessorEcEventCalled(
      fake_ui_message_receiver_diagnostics_processor(), 6, kFakeEcEventType1);
}

}  // namespace diagnostics
