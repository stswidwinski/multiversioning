#include "gtest/gtest.h"
#include "batch/lock_stage.h"
#include "test/test_lock_stage.h"
#include "test/test_action.h"

#include <thread>

bool operator==(const LockStage& ls1, const LockStage& ls2) {
  // validate requesters being identical.
  if (ls1.requesters.size() != ls2.requesters.size()) return false;

  auto ls1_req_iter = ls1.requesters.begin();
  auto ls2_req_iter = ls2.requesters.begin();
  for (unsigned int i = 0; i < ls1.requesters.size(); i++) {
    if (*(ls1_req_iter + i) != *(ls2_req_iter + i)) {
      return false;
    } 
  }

  return (
    ls1.holders == ls2.holders &&
    ls1.l_type == ls2.l_type);
}

class LockStageTest : public testing::Test {
private:
  std::vector<IBatchAction*> allocated_actions;
protected:
  void expect_holders_to_be(LockStage& ls, uint64_t n) {
    ASSERT_EQ(n, TestLockStage(ls).get_holders());
  }

  void expect_requesting_actions_to_be(LockStage& ls, const LockStage::RequestingActions& exp) {
    TestLockStage tls(ls);
    ASSERT_EQ(exp.size(), tls.get_requesters().size());
    auto exp_iter = exp.begin();
    auto tls_iter = ls.get_requesters().begin();
    for (unsigned int i = 0; i < exp.size(); i++) {
      EXPECT_EQ(*(exp_iter + i), *(tls_iter + i)); 
    };
  }

  void expect_lock_type_to_be(LockStage& ls, LockType lt) {
    ASSERT_EQ(lt, TestLockStage(ls).get_lock_type());
  }

  void expect_lock_stages_to_equal(LockStage& ls1, LockStage& ls2) {
    ASSERT_EQ(TestLockStage(ls1), TestLockStage(ls2));
  }

  TestAction* get_action() {
    allocated_actions.push_back(new TestAction(new TestTxn));
    return static_cast<TestAction*>(allocated_actions.back());
  }

  virtual void TearDown() {
    for (auto& act_ptr : allocated_actions) {
      delete act_ptr;
    }
  }
};

TEST_F(LockStageTest, constructorTests) {
  LockStage ls1;
  expect_holders_to_be(ls1, 0);
  expect_requesting_actions_to_be(ls1, LockStage::RequestingActions({}));
  expect_lock_type_to_be(ls1, LockType::shared);

  TestAction* requester = get_action(); 
  LockStage ls2(
      {requester},
      LockType::exclusive);
  expect_holders_to_be(ls2, 1);
  expect_requesting_actions_to_be(ls2, {requester});
  expect_lock_type_to_be(ls2, LockType::exclusive);
}

// Adding more shared actions to a shared lock stage is OK. An attempt 
// to add an exlusive action to a shared lock stage should fail.
TEST_F(LockStageTest, addStageToSharedTest) {
  LockStage ls1;
  // insertion of a single shared stage to an empty lockstage
  ASSERT_TRUE(ls1.add_to_stage(
        get_action(),
        LockType::shared));
  expect_lock_type_to_be(ls1, LockType::shared);
  ASSERT_EQ(ls1.get_requesters().size(), 1);

  // insertion of another shared stage to the same lockstage
  ASSERT_TRUE(ls1.add_to_stage(
        get_action(),
        LockType::shared));
  expect_lock_type_to_be(ls1, LockType::shared);
  ASSERT_EQ(ls1.get_requesters().size(), 2);

  // attempt to insert an exlusive stage to the same lockstage
  LockStage expected(ls1);
  ASSERT_FALSE(ls1.add_to_stage(
        get_action(),
        LockType::exclusive));
  // the stage should not have changed
  expect_lock_stages_to_equal(expected, ls1); 
}

// Adding exclusive action to an empty lock stage is OK. Adding anything
// to an exclusive lock stage should fail.
TEST_F(LockStageTest, addStageToExclusiveTest) {
  LockStage ls1;
  ASSERT_TRUE(ls1.add_to_stage(
        get_action(),
        LockType::exclusive));
  ASSERT_EQ(TestLockStage(ls1).get_lock_type(), LockType::exclusive);
  ASSERT_EQ(ls1.get_requesters().size(), 1);

  // no matter what we try to add, we should fail and the lockstage will
  // not change
  LockStage expected(ls1);
  ASSERT_FALSE(ls1.add_to_stage(
        get_action(),
        LockType::exclusive));
  expect_lock_stages_to_equal(expected, ls1); 

  ASSERT_FALSE(ls1.add_to_stage(
        get_action(),
        LockType::shared));
  expect_lock_stages_to_equal(expected, ls1); 
}

TEST_F(LockStageTest, decrement_holdersTest) {
  // lock stage with 30 actions
  LockStage ls1;
  for (unsigned int i = 0; i < 30; i++) 
    ls1.add_to_stage(
        get_action(),
        LockType::shared);

  // single threaded decrement works.
  expect_holders_to_be(ls1, 30);
  ASSERT_EQ(29, ls1.decrement_holders());

  // launch two threads to decrement different number, but adding up to 99
  std::thread threads[2];
  threads[0] = std::thread([&ls1](){for(unsigned int i = 0; i < 15; i++) ls1.decrement_holders();});
  threads[1] = std::thread([&ls1](){for(unsigned int i = 0; i < 14; i++) ls1.decrement_holders();});

  threads[0].join();
  threads[1].join();
  expect_holders_to_be(ls1, 0);
} 

TEST_F(LockStageTest, notify_lock_obtainedTest) {
  LockStage ls;
  std::vector<TestAction*> acts;
  for (unsigned int i = 0; i < 3; i++) {
    auto act = get_action();
    act->add_write_key({i});
    acts.push_back(act);
    ls.add_to_stage(acts[i], LockType::shared);
  }
  ASSERT_FALSE(ls.has_lock());
  ls.notify_lock_obtained();
  ASSERT_TRUE(ls.has_lock());
  for (auto act_ptr : acts) {
    ASSERT_TRUE(act_ptr->ready_to_execute());
  }
};

TEST_F(LockStageTest, finalize_actionTest) {
  LockStage ls;
  std::vector<TestAction*> acts;
  for (unsigned int i = 0; i < 3; i++) {
    auto act = get_action();
    act->add_write_key({i});
    acts.push_back(act);
    ls.add_to_stage(acts[i], LockType::shared);
  }

  ls.notify_lock_obtained();
  for (unsigned int i = 0; i < 2; i++) {
    ASSERT_FALSE(ls.finalize_action(acts[i]));
  }

  ASSERT_TRUE(ls.finalize_action(acts[2]));
};
