#ifndef EXECUTOR_THREAD_H_
#define EXECUTOR_THREAD_H_

#include "batch/batch_action.h"
#include "runnable.hh"

class ExecutorThreadManager;
class ExecutorThread : public Runnable {
protected:
  ExecutorThreadManager* manager;

  ExecutorThread(
      ExecutorThreadManager* manager,
      int m_cpu_number):
    Runnable(m_cpu_number),
    manager(manager)
  {};
public:
  using Runnable::StartWorking;
  using Runnable::Init;

  // TODO: 
  //    Make this typedef in global schedule so that it makes more sense? 
  //    Also... I think this will require changes in lock stages? Lock Stages
  //    don't own actions alone... they share them with the execution threads!
  typedef std::vector<std::shared_ptr<BatchAction>> BatchActions;

  virtual void add_actions(BatchActions&& actions) = 0;
  virtual std::shared_ptr<BatchAction> try_get_done_action() = 0;
};

#endif // EXECUTOR_THREAD_H_
