#include "gtest/gtest.h"
#include "batch/scheduler_manager.h"
#include "test/test_action.h"
#include "test/test_executor_thread_manager.h"

#include <thread>
#include <memory>
#include <algorithm>

class SchedulerManagerTest :
  public testing::Test,
  public testing::WithParamInterface<int> {
protected:
  std::shared_ptr<SchedulerManager> sm;
  std::shared_ptr<ExecutorThreadManager> etm;
	const uint32_t batch_size = 100;
	const uint32_t batch_length_sec = 0;
	const uint32_t scheduling_threads_count = 3;

  const SchedulingSystemConfig conf = {
		scheduling_threads_count,
		batch_size,
		scheduling_threads_count
	};
	const unsigned int actions_at_start = batch_size * scheduling_threads_count;

	virtual void SetUp() {
    etm = std::make_shared<TestExecutorThreadManager>();
		sm = std::make_shared<SchedulerManager>(this->conf, etm.get());
		// populate the input queue.
		for (unsigned int i = 0;
				i < actions_at_start;
				i++) {
			sm->add_action(
				std::move(
					std::unique_ptr<TestAction>(
						TestAction::make_test_action_with_test_txn({}, {}, i))));
		}
	};
};	

void assertBatchIsCorrect(
		std::unique_ptr<std::vector<std::unique_ptr<IBatchAction>>>&& batch,
		unsigned int expected_size,
		unsigned int begin_id,
		unsigned int line) {
	ASSERT_EQ(expected_size, batch->size());
	TestAction* act;
	for (unsigned int i = begin_id; i < begin_id + expected_size; i++) {
		act = static_cast<TestAction*>((*batch)[i - begin_id].get());
		ASSERT_EQ(i, act->get_id()) <<
			"Error within test starting at line" << line;
	}
}

TEST_F(SchedulerManagerTest, obtain_batchNonConcurrentTest) {
	auto batch = sm->request_input(sm->schedulers[0].get());
	assertBatchIsCorrect(std::move(batch.batch), batch_size, 0, __LINE__);	
  ASSERT_EQ(batch.batch_id, 0);
};

typedef std::function<void (int)> concurrentFun;
void runConcurrentTest(
    concurrentFun fun,
		uint32_t threads_num) {
	std::thread threads[threads_num];
	for (int i = threads_num - 1; i >= 0; i--) {
		threads[i] = std::thread(fun, i);
	}
	
	for (unsigned int i = 0; i < threads_num; i++) threads[i].join();	
}	

concurrentFun get_obtain_batch_test_fun(
    std::vector<SchedulerThreadBatch>& batches,
    std::shared_ptr<SchedulerManager> sm, 
    int line) {
  return [&batches, sm, line](int i) {
    batches[i] = sm->request_input(sm->schedulers[i].get());
  };
}

void doObtainBatchTest(
    unsigned int thread_count,
    unsigned int batch_size,
    std::shared_ptr<SchedulerManager> sm) {
  std::vector<SchedulerThreadBatch> batches(thread_count);

  runConcurrentTest(
      get_obtain_batch_test_fun(batches, sm,  __LINE__),
      thread_count);

  std::sort(batches.begin(), batches.end(), 
      [] (const SchedulerThreadBatch& stb1, const SchedulerThreadBatch& stb2) {
        return stb1.batch_id > stb2.batch_id;
      });

  for (unsigned int i = 0; i < thread_count; i++) {
    ASSERT_EQ(i, batches[i].batch_id);
    assertBatchIsCorrect(
      std::move(batches[i].batch),
      batch_size,
      i * batch_size,
      __LINE__
    );
  }
};

TEST_P(SchedulerManagerTest, obtain_batchConcurrentTest1) {
  doObtainBatchTest(scheduling_threads_count - 1, batch_size, sm);
}

TEST_P(SchedulerManagerTest, obtain_batchConcurrentTest2) {
  doObtainBatchTest(scheduling_threads_count, batch_size, sm);
}

concurrentFun get_signal_exec_threads_test_fun(
    std::shared_ptr<SchedulerManager> sm) {
  return [sm](int i) {
    SchedulerManager::OrderedWorkload workload; 
    BatchLockTable blt;
    sm->hand_batch_to_execution(
        sm->schedulers[i].get(), i, std::move(workload), std::move(blt)); 
  };  
};

TEST_P(SchedulerManagerTest, signal_execution_threadsConcurrentNSOFTest) {
  runConcurrentTest(
      get_signal_exec_threads_test_fun(sm),
      scheduling_threads_count - 1);
  TestExecutorThreadManager* tetm = 
    static_cast<TestExecutorThreadManager*>(sm->exec_manager);
  ASSERT_EQ(scheduling_threads_count - 1, tetm->signal_execution_threads_called);
};

TEST_P(SchedulerManagerTest, signal_execution_threadsConcurrentSOFTest) {
  runConcurrentTest(
      get_signal_exec_threads_test_fun(sm),
      scheduling_threads_count);
  TestExecutorThreadManager* tetm = 
    static_cast<TestExecutorThreadManager*>(sm->exec_manager);
  ASSERT_EQ(scheduling_threads_count, tetm->signal_execution_threads_called);
};

INSTANTIATE_TEST_CASE_P(
	RerunForEdgeCond,
	SchedulerManagerTest,
	testing::Range(1, 50));
