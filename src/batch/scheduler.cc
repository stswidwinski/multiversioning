#include "batch/scheduler.h"
#include "batch/arr_container.h"
#include "batch/packing.h"
#include "util.h"

#include <cassert>

Scheduler::Scheduler(
    SchedulerThreadManager* manager,
    int m_cpu_number,
    uint64_t thread_id):
  SchedulerThread(manager, m_cpu_number, thread_id)
{};

void Scheduler::StartWorking() {
  while(!is_stop_requested()) {
    // get the batch actions
    batch_actions = std::make_unique<SchedulerThreadBatch>(
        std::move(this->manager->request_input(this)));
    process_batch();
    this->manager->hand_batch_to_execution(
        this, 
        batch_actions->batch_id, 
        std::move(workloads), 
        std::move(lt));
  }
};

void Scheduler::Init() {
};

void Scheduler::process_batch() {
  workloads = SchedulerThreadManager::OrderedWorkload(batch_actions->batch->size());
  lt = BatchLockTable();
  ArrayContainer ac(std::move(batch_actions->batch));

  // populate the batch lock table and workloads
  unsigned int curr_workload_item = 0;
  std::vector<std::unique_ptr<IBatchAction>> packing;
  while (ac.get_remaining_count() != 0) {
    // get packing
    packing = Packer::get_packing(&ac);
    ac.sort_remaining();
    // translate a packing into lock request
    for (std::unique_ptr<IBatchAction>& act : packing) {
      auto act_sptr = std::shared_ptr<IBatchAction>(std::move(act));
      workloads[curr_workload_item++] = act_sptr;
      lt.insert_lock_request(act_sptr);
    }
  }

  assert(curr_workload_item == workloads.size());
};

Scheduler::~Scheduler() {
};

void Scheduler::signal_stop_working() {
  xchgq(&stop_signal, 1);
}

bool Scheduler::is_stop_requested() {
  return stop_signal;
}
