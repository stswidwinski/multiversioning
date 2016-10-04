#include <occ.h>
#include <action.h>
#include <cpuinfo.h>
#include <algorithm>

OCCWorker::OCCWorker(OCCWorkerConfig conf, struct RecordBuffersConfig rb_conf)
        : Runnable(conf.cpu)
{
        this->config = conf;
        this->bufs = new(conf.cpu) RecordBuffers(rb_conf);
        this->mgr = new(conf.cpu) mcs_mgr(NUM_MCS_LOCKS, conf.cpu);
        this->insert_mgr = new(conf.cpu) insert_buf_mgr(conf.cpu, 11, 
                                                        insert_tpcc_record_sizes,
                                                        true);
        this->key_allocator = new(conf.cpu) array_allocator<occ_composite_key>(100, conf.cpu);
}

void OCCWorker::Init()
{
}

/*
 * Process batches of transactions.
 */
void OCCWorker::StartWorking()
{
        if (config.cpu == 0) {
                EpochManager();
        } else {
                TxnRunner();
        }
}

void OCCWorker::EpochManager()
{
        uint64_t i, iters;
        iters = 100000;
        assert(iters < (config.epoch_threshold/2));
        barrier();
        incr_timestamp = rdtsc();
        barrier();
        while (true) {
                for (i = 0; i < iters; ++i) {
                        single_work();
                }
                UpdateEpoch();
        }
}

uint32_t OCCWorker::exec_pending(OCCAction **pending_list)
{
        OCCAction *cur, *prev;
        uint32_t num_done;
        
        prev = NULL;
        cur = *pending_list;
        num_done = 0;
        while (cur != NULL) {
                if (RunSingle(cur)) {
                        if (prev == NULL) 
                                *pending_list = cur->link;
                        else 
                                prev->link = cur->link;                        
                        num_done += 1;
                } else {
                        prev = cur;
                }                
                cur = cur->link;
        }
        return num_done;
}

void OCCWorker::TxnRunner()
{
        uint32_t i, j, num_pending;
        OCCActionBatch input, output;//, batches[2];
        OCCAction *pending_list;
        
        num_pending = 0;
        pending_list = NULL;
        output.batch = NULL;
        
        /* This is very hacky. For measurement purposes only!!! */
        if (config.cpu == 1) {
                input = config.inputQueue->DequeueBlocking();
                for (i = 0; i < input.batchSize; ++i) 
                        if (!RunSingle(input.batch[i]))
                                assert(false);
                config.outputQueue->EnqueueBlocking(input);                
        }
        
        barrier();
        config.num_completed = 0;
        barrier();

        for (j = 0; j < 3 ; ++j) {
                input = config.inputQueue->DequeueBlocking();
                if (j < 1) {
                        for (i = 0; i < input.batchSize; ++i) {
                                while (num_pending >= 50) 
                                        num_pending -= exec_pending(&pending_list);
                                if (!RunSingle(input.batch[i])) {
                                        input.batch[i]->link = pending_list;
                                        pending_list = input.batch[i];
                                        num_pending += 1;
                                } 
                        }
                        while (num_pending != 0) 
                                num_pending -= exec_pending(&pending_list);
                        assert(pending_list == NULL);
                        output.batchSize = input.batchSize;
                        config.outputQueue->EnqueueBlocking(output);
                } else {
                        uint32_t batch_sz = input.batchSize;
                        //                        uint64_t blah_lo = NumCompleted();
                        for (i = 0; ; ++i) {
                                //                                uint64_t blah_hi = NumCompleted();
                                //                                if (blah_hi - blah_lo > 10000) {
                                //                                        std::cout << "Thread: " << config.cpu << " Txns: " << blah_hi - blah_lo << "\n";
                                //                                        blah_lo = blah_hi;
                                //                                }
                                
                                while (num_pending >= 50)
                                        num_pending -= exec_pending(&pending_list);
                                if (!RunSingle(input.batch[i % batch_sz])) {
                                        input.batch[i % batch_sz]->link = pending_list;
                                        pending_list = input.batch[i % batch_sz];
                                        num_pending += 1;
                                }
                        }
                }
        }
        
}

void OCCWorker::UpdateEpoch()
{
        uint32_t temp;
        barrier();
        volatile uint64_t now = rdtsc();
        barrier();
        if (now - incr_timestamp > config.epoch_threshold) {                
                temp = fetch_and_increment_32(config.epoch_ptr);                
                incr_timestamp = now;
                assert(temp != 0);
        }
}

uint64_t OCCWorker::NumCompleted()
{
        uint64_t ret;
        barrier();
        ret = config.num_completed;
        barrier();
        return ret;
}

/*
 * Run the action to completion. If the transaction aborts due to a conflict, 
 * retry.
 */
bool OCCWorker::RunSingle(OCCAction *action)
{
        volatile uint32_t epoch;
        bool validated;

        action->set_tables(this->config.tables, this->config.lock_tables);
        action->set_allocator(this->bufs);
        action->set_mgr(this->mgr);
        action->worker = this;
        action->tbl_mgr = config.tbl_mgr;
        action->insert_mgr = insert_mgr;
        action->lck = mgr->get_struct();
        action->cpu_id = (uint32_t)config.cpu;
        action->key_allocator = key_allocator;
        
        action->inserted = NULL;
        action->run();
        action->acquire_locks();
        barrier();
        epoch = *config.epoch_ptr;
        barrier();                        
        
        if (action->validate() == true) {
                this->last_tid = action->compute_tid(epoch,
                                                     this->last_tid);
                action->install_writes();
                action->cleanup();
                fetch_and_increment(&config.num_completed);
                validated = true;
        } else {
                action->release_locks();
                action->undo_inserts();
                action->cleanup();
                validated = false;
        }
        mgr->return_struct(action->lck);
        key_allocator->reset();
        return validated;
}
