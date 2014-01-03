// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>

#include <base/file_path.h>
#include <base/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/message_loop_proxy.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_ptr.h>
#include <base/message_loop.h>
#include <base/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/browser_job.h"
#include "login_manager/fake_browser_job.h"
#include "login_manager/fake_child_process.h"
#include "login_manager/fake_generator_job.h"
#include "login_manager/mock_device_policy_service.h"
#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_key_generator.h"
#include "login_manager/mock_liveness_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_session_manager.h"
#include "login_manager/mock_system_utils.h"
#include "login_manager/system_utils_impl.h"

using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::AtMost;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Not;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::Sequence;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::WithArgs;
using ::testing::_;

using std::string;

namespace login_manager {

// Used as a fixture for the tests in this file.
// Gives useful shared functionality.
class SessionManagerProcessTest : public ::testing::Test {
 public:
  SessionManagerProcessTest()
      : manager_(NULL),
        file_checker_(new MockFileChecker(kCheckedFile)),
        liveness_checker_(new MockLivenessChecker),
        metrics_(new MockMetrics),
        session_manager_impl_(new MockSessionManager),
        must_destroy_mocks_(true) {
  }

  virtual ~SessionManagerProcessTest() {
    if (must_destroy_mocks_) {
      delete file_checker_;
      delete liveness_checker_;
      delete metrics_;
      delete session_manager_impl_;
    }
  }

  virtual void SetUp() {
    ASSERT_TRUE(tmpdir_.CreateUniqueTempDir());
  }

  virtual void TearDown() {
    must_destroy_mocks_ = !manager_.get();
    manager_ = NULL;
  }

 protected:
  // kFakeEmail is NOT const so that it can be passed to methods that
  // implement dbus calls, which (of necessity) take bare gchar*.
  static char kFakeEmail[];
  static const char kCheckedFile[];
  static const pid_t kDummyPid;
  static const int kExit;

  void MockUtils() {
    manager_->test_api().set_systemutils(&utils_);
  }

  void ExpectSuccessfulInitialization() {
    // Happy signal needed during Run().
    EXPECT_CALL(*session_manager_impl_, Initialize()).WillOnce(Return(true));
  }

  void ExpectShutdown() {
    EXPECT_CALL(*session_manager_impl_, Finalize())
        .Times(1);
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStoppingIfNeeded())
        .Times(1);
    EXPECT_CALL(*session_manager_impl_, AnnounceSessionStopped())
        .Times(AtMost(1));
  }

  void ExpectLivenessChecking() {
    EXPECT_CALL(*liveness_checker_, Start()).Times(AtLeast(1));
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(1));
  }

  void InitManager(FakeBrowserJob* job) {
    ASSERT_TRUE(MessageLoop::current() == NULL);
    manager_ = new SessionManagerService(scoped_ptr<BrowserJobInterface>(job),
                                         3,
                                         false,
                                         base::TimeDelta(),
                                         &real_utils_);
    manager_->Reset();
    manager_->set_file_checker(file_checker_);
    manager_->test_api().set_liveness_checker(liveness_checker_);
    manager_->test_api().set_login_metrics(metrics_);
    manager_->test_api().set_session_manager(session_manager_impl_);
  }

  void SimpleRunManager() {
    ExpectSuccessfulInitialization();
    ExpectShutdown();

    // Expect and mimic successful cleanup of children.
    EXPECT_CALL(utils_, kill(_, _, _))
        .WillRepeatedly(WithArgs<0,2>(Invoke(::kill)));
    EXPECT_CALL(utils_, ChildIsGone(_, _))
        .WillRepeatedly(Return(true));

    MockUtils();
    manager_->Run();
  }

  FakeBrowserJob* CreateMockJobAndInitManager(bool schedule_exit) {
    FakeBrowserJob* job = new FakeBrowserJob("FakeBrowserJob", schedule_exit);
    InitManager(job);

    job->set_fake_child_process(
        scoped_ptr<FakeChildProcess>(
            new FakeChildProcess(kDummyPid, 0, manager_->test_api())));

    EXPECT_CALL(*file_checker_, exists())
        .WillRepeatedly(Return(false))
        .RetiresOnSaturation();
    return job;
  }

  int PackStatus(int status) { return __W_EXITCODE(status, 0); }
  int PackSignal(int signal) { return __W_EXITCODE(0, signal); }

  scoped_refptr<SessionManagerService> manager_;
  SystemUtilsImpl real_utils_;
  MockSystemUtils utils_;

  // These are bare pointers, not scoped_ptrs, because we need to give them
  // to a SessionManagerService instance, but also be able to set expectations
  // on them after we hand them off.
  MockFileChecker* file_checker_;
  MockLivenessChecker* liveness_checker_;
  MockMetrics* metrics_;
  MockSessionManager* session_manager_impl_;

 private:
  bool must_destroy_mocks_;
  base::ScopedTempDir tmpdir_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerProcessTest);
};

// static
char SessionManagerProcessTest::kFakeEmail[] = "cmasone@whaaat.org";
const char SessionManagerProcessTest::kCheckedFile[] = "/tmp/checked_file";
const pid_t SessionManagerProcessTest::kDummyPid = 4;
const int SessionManagerProcessTest::kExit = 1;

TEST_F(SessionManagerProcessTest, CleanupChildren) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  job->RunInBackground();
  manager_->test_api().CleanupChildren(3);
}

TEST_F(SessionManagerProcessTest, CleanupSeveralChildren) {
  FakeBrowserJob* browser_job = CreateMockJobAndInitManager(false);
  browser_job->RunInBackground();

  pid_t generator_pid = kDummyPid + 1;
  scoped_ptr<FakeGeneratorJob> generator(new FakeGeneratorJob(generator_pid,
                                                              "Generator"));
  EXPECT_CALL(*browser_job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*generator.get(), Kill(SIGTERM, _)).Times(1);

  manager_->AdoptKeyGeneratorJob(
      scoped_ptr<GeneratorJobInterface>(generator.Pass()), generator_pid);

  manager_->test_api().CleanupChildren(3);
}

TEST_F(SessionManagerProcessTest, SlowKillCleanupChildren) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);
  job->RunInBackground();
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(*job, KillEverything(SIGABRT, _)).Times(1);

  EXPECT_CALL(utils_, ChildIsGone(job->CurrentPid(), _))
      .WillOnce(Return(false));
  MockUtils();

  manager_->test_api().CleanupChildren(3);
}

TEST_F(SessionManagerProcessTest, SessionStartedCleanup) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);

  // Expect the job to be faux-run.
  ExpectLivenessChecking();

  ExpectSuccessfulInitialization();
  ExpectShutdown();

  // Expect the job to be killed, and die promptly.
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(utils_, ChildIsGone(kDummyPid, _))
      .WillOnce(Return(true));

  MockUtils();

  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&SessionManagerService::ScheduleShutdown),
                 manager_.get()));
  manager_->Run();
}

TEST_F(SessionManagerProcessTest, SessionStartedSlowKillCleanup) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(false);

  // Expect the job to be faux-run.
  ExpectLivenessChecking();

  ExpectSuccessfulInitialization();
  ExpectShutdown();

  base::TimeDelta timeout = base::TimeDelta::FromSeconds(3);
  EXPECT_CALL(*job, Kill(SIGTERM, _)).Times(1);
  EXPECT_CALL(utils_, ChildIsGone(kDummyPid, timeout))
      .WillOnce(Return(false));
  EXPECT_CALL(*job, KillEverything(SIGABRT, _)).Times(1);

  MockUtils();

  base::MessageLoopProxy::current()->PostTask(
      FROM_HERE,
      base::Bind(base::IgnoreResult(&SessionManagerService::ScheduleShutdown),
                 manager_.get()));
  manager_->Run();
}

TEST_F(SessionManagerProcessTest, BadExitChildFlagFileStop) {
  FakeBrowserJob* job = new FakeBrowserJob("BadExit");
  ExpectLivenessChecking();
  InitManager(job);
  // So that the manager will exit, even though it'd normally run forever.
  manager_->test_api().set_exit_on_child_done(true);

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(false));
  EXPECT_CALL(*file_checker_, exists())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));

  job->set_fake_child_process(
      scoped_ptr<FakeChildProcess>(
          new FakeChildProcess(kDummyPid,
                               PackStatus(kExit),
                               manager_->test_api())));
  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, BadExitChildOnSignal) {
  FakeBrowserJob* job = new FakeBrowserJob("BadExit");
  ExpectLivenessChecking();
  InitManager(job);

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop()).WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));

  job->set_fake_child_process(
      scoped_ptr<FakeChildProcess>(
          new FakeChildProcess(kDummyPid,
                               PackSignal(SIGILL),
                               manager_->test_api())));
  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, BadExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));

  job->set_fake_child_process(
      scoped_ptr<FakeChildProcess>(
          new FakeChildProcess(kDummyPid,
                               PackStatus(kExit),
                               manager_->test_api())));
  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, CleanExitChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop())
      .WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));

  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, LockedExit) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();

  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop())
      .Times(0);

  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillOnce(Return(true));
  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, LivenessCheckingStartStop) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  {
    Sequence start_stop;
    EXPECT_CALL(*liveness_checker_, Start()).Times(2);
    EXPECT_CALL(*liveness_checker_, Stop()).Times(AtLeast(2));
  }
  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));

  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, MustStopChild) {
  FakeBrowserJob* job = CreateMockJobAndInitManager(true);
  ExpectLivenessChecking();
  EXPECT_CALL(*job, KillEverything(SIGKILL, _)).Times(AnyNumber());
  EXPECT_CALL(*job, ShouldStop())
      .WillOnce(Return(true));
  EXPECT_CALL(*session_manager_impl_, ScreenIsLocked())
      .WillRepeatedly(Return(false));
  SimpleRunManager();
}

TEST_F(SessionManagerProcessTest, TestWipeOnBadState) {
  CreateMockJobAndInitManager(true);

  // Expected to occur during manager_->Run().
  EXPECT_CALL(*metrics_, HasRecordedChromeExec())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*metrics_, RecordStats(StrEq(("chrome-exec"))))
      .Times(AnyNumber());
  EXPECT_CALL(*session_manager_impl_, Initialize())
    .WillOnce(Return(false));
  EXPECT_CALL(*liveness_checker_, Stop()).Times(AnyNumber());

  // Expect Powerwash to be triggered.
  EXPECT_CALL(*session_manager_impl_, StartDeviceWipe(_, _))
    .WillOnce(Return(true));
  ExpectShutdown();
  MockUtils();

  ASSERT_FALSE(manager_->Run());
}

}  // namespace login_manager
