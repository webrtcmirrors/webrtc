/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#if defined(WEBRTC_WIN)
// clang-format off
#include <windows.h>  // Must come first.
#include <mmsystem.h>
// clang-format on
#endif

#include <stdint.h>
#include <memory>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "rtc_base/bind.h"
#include "rtc_base/event.h"
#include "rtc_base/task_queue.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/time_utils.h"
#include "test/gtest.h"

using rtc::test::TaskQueueForTest;

namespace rtc {

namespace {
// Noop on all platforms except Windows, where it turns on high precision
// multimedia timers which increases the precision of TimeMillis() while in
// scope.
class EnableHighResTimers {
 public:
#if !defined(WEBRTC_WIN)
  EnableHighResTimers() {}
#else
  EnableHighResTimers() : enabled_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
  ~EnableHighResTimers() {
    if (enabled_)
      timeEndPeriod(1);
  }

 private:
  const bool enabled_;
#endif
};

void CheckCurrent(Event* signal, TaskQueue* queue) {
  EXPECT_TRUE(queue->IsCurrent());
  if (signal)
    signal->Set();
}

}  // namespace

TEST(TaskQueueTest, Construct) {
  static const char kQueueName[] = "Construct";
  TaskQueue queue(kQueueName);
  EXPECT_FALSE(queue.IsCurrent());
}

TEST(TaskQueueTest, PostAndCheckCurrent) {
  static const char kQueueName[] = "PostAndCheckCurrent";
  Event event;
  TaskQueue queue(kQueueName);

  // We're not running a task, so there shouldn't be a current queue.
  EXPECT_FALSE(queue.IsCurrent());
  EXPECT_FALSE(TaskQueue::Current());

  queue.PostTask(Bind(&CheckCurrent, &event, &queue));
  EXPECT_TRUE(event.Wait(1000));
}

TEST(TaskQueueTest, PostCustomTask) {
  static const char kQueueName[] = "PostCustomImplementation";
  TaskQueueForTest queue(kQueueName);

  class CustomTask : public QueuedTask {
   public:
    CustomTask() {}
    bool ran() const { return ran_; }

   private:
    bool Run() override {
      ran_ = true;
      return false;  // Never allow the task to be deleted by the queue.
    }

    bool ran_ = false;
  } my_task;

  queue.SendTask(&my_task);
  EXPECT_TRUE(my_task.ran());
}

TEST(TaskQueueTest, PostLambda) {
  TaskQueueForTest queue("PostLambda");
  bool ran = false;
  queue.SendTask([&ran]() { ran = true; });
  EXPECT_TRUE(ran);
}

TEST(TaskQueueTest, PostDelayedZero) {
  static const char kQueueName[] = "PostDelayedZero";
  Event event;
  TaskQueue queue(kQueueName);

  queue.PostDelayedTask([&event]() { event.Set(); }, 0);
  EXPECT_TRUE(event.Wait(1000));
}

TEST(TaskQueueTest, PostFromQueue) {
  static const char kQueueName[] = "PostFromQueue";
  Event event;
  TaskQueue queue(kQueueName);

  queue.PostTask(
      [&event, &queue]() { queue.PostTask([&event]() { event.Set(); }); });
  EXPECT_TRUE(event.Wait(1000));
}

TEST(TaskQueueTest, PostDelayed) {
  static const char kQueueName[] = "PostDelayed";
  Event event;
  TaskQueue queue(kQueueName, TaskQueue::Priority::HIGH);

  uint32_t start = Time();
  queue.PostDelayedTask(Bind(&CheckCurrent, &event, &queue), 100);
  EXPECT_TRUE(event.Wait(1000));
  uint32_t end = Time();
  // These tests are a little relaxed due to how "powerful" our test bots can
  // be.  Most recently we've seen windows bots fire the callback after 94-99ms,
  // which is why we have a little bit of leeway backwards as well.
  EXPECT_GE(end - start, 90u);
  EXPECT_NEAR(end - start, 190u, 100u);  // Accept 90-290.
}

// This task needs to be run manually due to the slowness of some of our bots.
// TODO(tommi): Can we run this on the perf bots?
TEST(TaskQueueTest, DISABLED_PostDelayedHighRes) {
  EnableHighResTimers high_res_scope;

  static const char kQueueName[] = "PostDelayedHighRes";
  Event event;
  TaskQueue queue(kQueueName, TaskQueue::Priority::HIGH);

  uint32_t start = Time();
  queue.PostDelayedTask(Bind(&CheckCurrent, &event, &queue), 3);
  EXPECT_TRUE(event.Wait(1000));
  uint32_t end = TimeMillis();
  // These tests are a little relaxed due to how "powerful" our test bots can
  // be.  Most recently we've seen windows bots fire the callback after 94-99ms,
  // which is why we have a little bit of leeway backwards as well.
  EXPECT_GE(end - start, 3u);
  EXPECT_NEAR(end - start, 3, 3u);
}

TEST(TaskQueueTest, PostMultipleDelayed) {
  static const char kQueueName[] = "PostMultipleDelayed";
  TaskQueue queue(kQueueName);

  std::vector<std::unique_ptr<Event>> events;
  for (int i = 0; i < 100; ++i) {
    events.push_back(absl::make_unique<Event>());
    queue.PostDelayedTask(Bind(&CheckCurrent, events.back().get(), &queue), i);
  }

  for (const auto& e : events)
    EXPECT_TRUE(e->Wait(1000));
}

TEST(TaskQueueTest, PostDelayedAfterDestruct) {
  static const char kQueueName[] = "PostDelayedAfterDestruct";
  Event run;
  Event deleted;
  {
    TaskQueue queue(kQueueName);
    queue.PostDelayedTask(
        rtc::NewClosure([&run] { run.Set(); }, [&deleted] { deleted.Set(); }),
        100);
  }
  // Task might outlive the TaskQueue, but still should be deleted.
  EXPECT_TRUE(deleted.Wait(200));
  EXPECT_FALSE(run.Wait(0));  // and should not run.
}

TEST(TaskQueueTest, PostAndReuse) {
  static const char kPostQueue[] = "PostQueue";
  static const char kReplyQueue[] = "ReplyQueue";
  Event event;
  TaskQueue post_queue(kPostQueue);
  TaskQueue reply_queue(kReplyQueue);

  int call_count = 0;

  class ReusedTask : public QueuedTask {
   public:
    ReusedTask(int* counter, TaskQueue* reply_queue, Event* event)
        : counter_(counter), reply_queue_(reply_queue), event_(event) {
      EXPECT_EQ(0, *counter_);
    }

   private:
    bool Run() override {
      if (++(*counter_) == 1) {
        std::unique_ptr<QueuedTask> myself(this);
        reply_queue_->PostTask(std::move(myself));
        // At this point, the object is owned by reply_queue_ and it's
        // theoratically possible that the object has been deleted (e.g. if
        // posting wasn't possible).  So, don't touch any member variables here.

        // Indicate to the current queue that ownership has been transferred.
        return false;
      } else {
        EXPECT_EQ(2, *counter_);
        EXPECT_TRUE(reply_queue_->IsCurrent());
        event_->Set();
        return true;  // Indicate that the object should be deleted.
      }
    }

    int* const counter_;
    TaskQueue* const reply_queue_;
    Event* const event_;
  };

  std::unique_ptr<ReusedTask> task(
      new ReusedTask(&call_count, &reply_queue, &event));

  post_queue.PostTask(std::move(task));
  EXPECT_TRUE(event.Wait(1000));
}

TEST(TaskQueueTest, PostCopyableClosure) {
  struct CopyableClosure {
    CopyableClosure(int* num_copies, int* num_moves, Event* event)
        : num_copies(num_copies), num_moves(num_moves), event(event) {}
    CopyableClosure(const CopyableClosure& other)
        : num_copies(other.num_copies),
          num_moves(other.num_moves),
          event(other.event) {
      ++*num_copies;
    }
    CopyableClosure(CopyableClosure&& other)
        : num_copies(other.num_copies),
          num_moves(other.num_moves),
          event(other.event) {
      ++*num_moves;
    }
    void operator()() { event->Set(); }

    int* num_copies;
    int* num_moves;
    Event* event;
  };

  int num_copies = 0;
  int num_moves = 0;
  Event event;

  static const char kPostQueue[] = "PostCopyableClosure";
  TaskQueue post_queue(kPostQueue);
  {
    CopyableClosure closure(&num_copies, &num_moves, &event);
    post_queue.PostTask(closure);
    // Destroy closure to check with msan and tsan posted task has own copy.
  }

  EXPECT_TRUE(event.Wait(1000));
  EXPECT_EQ(num_copies, 1);
  EXPECT_EQ(num_moves, 0);
}

TEST(TaskQueueTest, PostMoveOnlyClosure) {
  struct SomeState {
    explicit SomeState(Event* event) : event(event) {}
    ~SomeState() { event->Set(); }
    Event* event;
  };
  struct MoveOnlyClosure {
    MoveOnlyClosure(int* num_moves, std::unique_ptr<SomeState> state)
        : num_moves(num_moves), state(std::move(state)) {}
    MoveOnlyClosure(const MoveOnlyClosure&) = delete;
    MoveOnlyClosure(MoveOnlyClosure&& other)
        : num_moves(other.num_moves), state(std::move(other.state)) {
      ++*num_moves;
    }
    void operator()() { state.reset(); }

    int* num_moves;
    std::unique_ptr<SomeState> state;
  };

  int num_moves = 0;
  Event event;
  std::unique_ptr<SomeState> state(new SomeState(&event));

  static const char kPostQueue[] = "PostMoveOnlyClosure";
  TaskQueue post_queue(kPostQueue);
  post_queue.PostTask(MoveOnlyClosure(&num_moves, std::move(state)));

  EXPECT_TRUE(event.Wait(1000));
  EXPECT_EQ(num_moves, 1);
}

TEST(TaskQueueTest, PostMoveOnlyCleanup) {
  struct SomeState {
    explicit SomeState(Event* event) : event(event) {}
    ~SomeState() { event->Set(); }
    Event* event;
  };
  struct MoveOnlyClosure {
    void operator()() { state.reset(); }

    std::unique_ptr<SomeState> state;
  };

  Event event_run;
  Event event_cleanup;
  std::unique_ptr<SomeState> state_run(new SomeState(&event_run));
  std::unique_ptr<SomeState> state_cleanup(new SomeState(&event_cleanup));

  static const char kPostQueue[] = "PostMoveOnlyCleanup";
  TaskQueue post_queue(kPostQueue);
  post_queue.PostTask(NewClosure(MoveOnlyClosure{std::move(state_run)},
                                 MoveOnlyClosure{std::move(state_cleanup)}));

  EXPECT_TRUE(event_cleanup.Wait(1000));
  // Expect run closure to complete before cleanup closure.
  EXPECT_TRUE(event_run.Wait(0));
}

// Tests posting more messages than a queue can queue up.
// In situations like that, tasks will get dropped.
TEST(TaskQueueTest, PostALot) {
  // To destruct the event after the queue has gone out of scope.
  Event event;

  int tasks_executed = 0;
  int tasks_cleaned_up = 0;
  static const int kTaskCount = 0xffff;

  {
    static const char kQueueName[] = "PostALot";
    TaskQueue queue(kQueueName);

    // On linux, the limit of pending bytes in the pipe buffer is 0xffff.
    // So here we post a total of 0xffff+1 messages, which triggers a failure
    // case inside of the libevent queue implementation.

    queue.PostTask([&event]() { event.Wait(Event::kForever); });
    for (int i = 0; i < kTaskCount; ++i)
      queue.PostTask(NewClosure([&tasks_executed]() { ++tasks_executed; },
                                [&tasks_cleaned_up]() { ++tasks_cleaned_up; }));
    event.Set();  // Unblock the first task.
  }

  EXPECT_GE(tasks_cleaned_up, tasks_executed);
  EXPECT_EQ(kTaskCount, tasks_cleaned_up);
}

// Test posting two tasks that have shared state not protected by a
// lock. The TaskQueue should guarantee memory read-write order and
// FIFO task execution order, so the second task should always see the
// changes that were made by the first task.
//
// If the TaskQueue doesn't properly synchronize the execution of
// tasks, there will be a data race, which is undefined behavior. The
// EXPECT calls may randomly catch this, but to make the most of this
// unit test, run it under TSan or some other tool that is able to
// directly detect data races.
TEST(TaskQueueTest, PostTwoWithSharedUnprotectedState) {
  static const char kQueueName[] = "PostTwoWithSharedUnprotectedState";
  struct SharedState {
    // First task will set this value to 1 and second will assert it.
    int state = 0;
  } state;

  TaskQueue queue(kQueueName);
  rtc::Event done;
  queue.PostTask([&state, &queue, &done] {
    // Post tasks from queue to guarantee, that 1st task won't be
    // executed before the second one will be posted.
    queue.PostTask([&state] { state.state = 1; });
    queue.PostTask([&state, &done] {
      EXPECT_EQ(state.state, 1);
      done.Set();
    });
    // Check, that state changing tasks didn't start yet.
    EXPECT_EQ(state.state, 0);
  });
  EXPECT_TRUE(done.Wait(1000));
}

}  // namespace rtc
