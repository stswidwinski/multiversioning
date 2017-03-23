#include "batch/global_schedule.h"

#include <cassert>

GlobalSchedule::GlobalSchedule();

inline
void GlobalSchedule::merge_into_global_schedule(
    BatchLockTable&& blt) {
  lt.merge_batch_table(blt);
};

LockStage* GlobalSchedule::get_stage_holding_lock_for(
    BatchAction::RecKey key) {
  return lt.get_head_for_record(key);
};

GlobalSchedule::finalize_execution_of_action(std::shared_ptr<BatchAction> act) {
  auto finalize_from_set = [this](BatchAction::RecSet* s){
    LockStage *ls;
    for (auto& key : *write_set) {
      ls = get_stage_holding_lock_for(key);

      if (ls->finalize_action(act)) {
        // if all the actions within the lockstage have finished,
        // move on and signal the next stage.
        lt.pass_lock_to_next_stage_for(key);
      }
    }   
  };

  finalize_from_set(act->get_readset_handle());
  finalize_from_set(act->get_writeset_handle());
};