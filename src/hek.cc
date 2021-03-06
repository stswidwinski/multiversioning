#include <hek.h>
#include <hek_table.h>
#include <hek_action.h>
#include <hek_record.h>

static void init_list(char *start, uint32_t num_records, uint32_t record_sz)
{
        uint32_t i;
        hek_record *cur, *next;
        
        assert(num_records > 0);
        cur = NULL;
        next = NULL;
        for (i = 0; i < num_records; ++i) {
                cur = (hek_record*)start;
                start += record_sz + sizeof(hek_record);
                next = (hek_record*)start;
                cur->next = next;
        }
        cur->next = NULL;
}

uint64_t hek_worker::get_timestamp()
{
        return fetch_and_increment(config.global_time);
        //        uint64_t ts = rdtsc();
        //        return (ts << 8);
}

/*
uint64_t hek_worker::read_timestamp()
{
        
}
*/

/*
 * Initialize the record allocator. Works in two phases, first do the 
 * allocation, then link up everything.
 */
void hek_worker::init_allocator()
{
        uint32_t record_sz, header_sz, i;
        char *temp, *start;
        uint64_t total_sz, free_list_sz, num_elems;

        header_sz = sizeof(hek_record);
        total_sz = config.num_tables*sizeof(hek_record*);        
        for (i = 0; i < this->config.num_tables; ++i) {
                free_list_sz = config.free_list_sizes[i];
                record_sz = config.record_sizes[i];
                num_elems = free_list_sz / (record_sz + header_sz);
                total_sz += num_elems * (record_sz + header_sz);
        }
        temp = (char*)alloc_interleaved_all(total_sz);
        //        temp = (char*)alloc_mem(total_sz, config.cpu);
        records = (hek_record**)temp;
        start = temp + config.num_tables*sizeof(hek_record*);
        for (i = 0; i < this->config.num_tables; ++i) {
                records[i] = (hek_record*)start;
                free_list_sz = config.free_list_sizes[i];
                record_sz = config.record_sizes[i];
                num_elems = free_list_sz / (record_sz + header_sz);
                init_list(start, num_elems, record_sz);
                start += num_elems * (record_sz + header_sz);
        }
}

hek_worker::hek_worker(hek_worker_config config) : Runnable(config.cpu)
{
        this->config = config;
        init_allocator();
}



void hek_worker::insert_commit_queue(hek_action *txn)
{
        int me;
        me = config.cpu;
        SimpleQueue<hek_action*> *queue = txn->worker->config.commit_queues[me];
        queue->EnqueueBlocking(txn);
}

void hek_worker::insert_abort_queue(hek_action *txn)
{
        int me;
        me = config.cpu;
        SimpleQueue<hek_action*> *queue = txn->worker->config.abort_queues[me];
        queue->EnqueueBlocking(txn);
}

void hek_worker::Init()
{
}

// A hek_queue is used to communicate the result of a commit dependency to a
// dependent transaction.
hek_queue::hek_queue()
{
        this->head = NULL;
        this->tail = &head;
        this->in_count = 0;
        this->out_count = 0;
}

// Insert a single transaction into the queue. First change the tail, then add a
// link pointer. Non-blocking.
void hek_queue::enqueue(hek_action *txn)
{
        volatile hek_action **prev;
        barrier();
        txn->next = NULL;
        barrier();
        prev = (volatile hek_action**)xchgq((volatile uint64_t*)&this->tail,
                                            (uint64_t)&txn->next);
        fetch_and_increment(&in_count);
        *prev = txn;
}

// Dequeue several transactions. 
hek_action* hek_queue::dequeue_batch()
{
        hek_action *ret, **old_tail;
        volatile hek_action *iter, *temp;
        iter = (volatile hek_action*)xchgq((volatile uint64_t*)&head, (uint64_t)NULL);
        ret = (hek_action*)iter;
        old_tail = (hek_action**)xchgq((volatile uint64_t*)&this->tail,
                                       (uint64_t)&head);
        if (iter != NULL) {
                assert(old_tail != &head);
                //                *old_tail = NULL;
                ++out_count;
                while (old_tail != &iter->next) {
                        while (true) {
                                barrier();
                                temp = iter->next;
                                barrier();
                                if (temp != NULL)
                                        break;
                        }
                        ++out_count;
                        iter = (volatile hek_action*)iter->next;
                }
        }

        /*
        iter = ret;
        while (iter != NULL) {
                ++out_count;
                iter = iter->next;
                }
        */
        return ret;
}

void hek_worker::abort_dependent(hek_action *aborted)
{
        assert(HEK_STATE(aborted->end) == PREPARING &&
               aborted->dep_flag == ABORT);
        transition_abort(aborted);
}

void hek_worker::commit_dependent(hek_action *committed)
{
        assert(HEK_STATE(committed->end) == PREPARING &&
               committed->dep_flag == COMMIT &&
               committed->dep_count == 0);
        transition_commit(committed);
 }

// Check the result of dependent transactions.
void hek_worker::check_dependents()
{
        hek_action *aborted, *committed;
        uint32_t i;

        for (i = 0; i < config.num_threads; ++i) {
                //                if (i != config.cpu) {
                        while (config.abort_queues[i]->Dequeue(&aborted))
                                abort_dependent(aborted);
                        //                } else {
                        //                        assert(config.abort_queues[i]->Dequeue(&aborted)
                        //                               == false);
                        //                }
        }

        for (i = 0; i < config.num_threads; ++i) {
                //                if (i != config.cpu) {
                        while (config.commit_queues[i]->Dequeue(&committed))
                                commit_dependent(committed);
                        //                } else {
                        //                        assert(config.commit_queues[i]->Dequeue(&committed)
                        //                               == false);
                        //                }
        }
}

// Hekaton worker threads's "main" function.
void hek_worker::StartWorking()
{
        uint32_t i;
        struct hek_batch input_batch, output_batch;
        
        output_batch.txns = NULL;
        while (true) {
                num_committed = 0;
                num_done = 0;
                input_batch = config.input_queue->DequeueBlocking();
                for (i = 0; i < input_batch.num_txns; ++i) {
                        run_txn(input_batch.txns[i]);
                        check_dependents();                
                }

                /* Wait for all txns with commit dependencies. */
                while (num_done != input_batch.num_txns) 
                        check_dependents();
                output_batch.num_txns = num_committed;
                config.output_queue->EnqueueBlocking(output_batch);
        }
}

//
// A transition can proceed only after acquiring the transaction's latch.
// A transaction's state changes from EXECUTING->PREPARING->COMMITTED/ABORTED.
// Commit dependencies can only be added in state PREPARING.
//
void hek_worker::transition_begin(__attribute__((unused)) hek_action *txn)
{
        
        //        lock(&txn->latch);
        //        txn->end = EXECUTING;
        //        unlock(&txn->latch);
        //        assert(CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->begin) &&
        //               CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->end));
}

void hek_worker::transition_preparing(hek_action *txn)
{
        uint64_t end_ts;
        end_ts = get_timestamp();
        end_ts = CREATE_PREP_TIMESTAMP(end_ts);
        lock(&txn->latch);
        //        txn->end = end_ts;
        xchgq(&txn->end, end_ts);
        unlock(&txn->latch);
        assert(HEK_TIME(txn->end) > HEK_TIME(txn->begin));

        // Reading global timestamp kills performance. Use the following
        // assertion only for debugging purposes.
        //        assert(CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->begin) &&
        //               CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->end));
}

void hek_worker::transition_commit(hek_action *txn)
{
        uint64_t time;
        assert(HEK_STATE(txn->end) == PREPARING);
        time = HEK_TIME(txn->end);
        time |= COMMIT;
        lock(&txn->latch);
        xchgq(&txn->end, time);
        //        txn->end = time;
        do_commit(txn);        
        unlock(&txn->latch);
        assert(HEK_TIME(txn->end) > HEK_TIME(txn->begin));

        // Reading global timestamp kills performance. Use the following
        // assertion only for debugging purposes.
        //        assert(CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->begin) &&
        //               CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->end));
}

void hek_worker::transition_abort(hek_action *txn)
{
        uint64_t time;
        
        assert(HEK_STATE(txn->end) == PREPARING);
        time = HEK_TIME(txn->end);
        time |= ABORT;
        lock(&txn->latch);        
        //        txn->end = time;
        xchgq(&txn->end, time);
        do_abort(txn);        
        unlock(&txn->latch);
        assert(HEK_TIME(txn->end) > HEK_TIME(txn->begin));

        // Reading global timestamp kills performance. Use the following
        // assertion only for debugging purposes.
        //        assert(CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->begin) &&
        //               CREATE_EXEC_TIMESTAMP(*config.global_time) >= HEK_TIME(txn->end));
}

/* Give a transaction a new record for every write it performs. */
void hek_worker::get_writes(hek_action *txn)
{
        uint32_t num_writes, i, table_id;
        struct hek_record *write_record;

        num_writes = txn->writeset.size();
        for (i = 0; i < num_writes; ++i) {
                table_id = txn->writeset[i].table_id;
                write_record = get_new_record(table_id);
                write_record->key = txn->writeset[i].key;
                txn->writeset[i].value = write_record;
        }
}

//
// Runs before txn logic begins. Keep a reference to every record read for
// validation.
//
void hek_worker::get_reads(hek_action *txn)
{
        uint32_t num_reads, i, table_id;
        uint64_t key, ts, *begin_ptr, *txn_ts;
        struct hek_record *read_record;
        
        ts = HEK_TIME(txn->begin);
        num_reads = txn->readset.size();
        for (i = 0; i < num_reads; ++i) {
                table_id = txn->readset[i].table_id;
                key = txn->readset[i].key;
                begin_ptr = &txn->readset[i].time;
                txn_ts = &txn->readset[i].txn_ts;
                read_record = config.tables[table_id]->get_version(key, ts,
                                                                   begin_ptr,
                                                                   txn_ts);
                txn->readset[i].value = read_record;
        }
}

/*
 * Each transaction's state is protected by a lock. We need locks because of
 * commit dependencies; a commit dependency can only be added if the transaction
 * is in state PREPARING. We can't atomically ensure that the transaction's
 * state is PREPARING and enqueue the commit dependency without locks.
 */
bool hek_worker::add_commit_dep(hek_action *out, hek_key *key, hek_action *in,
                                uint64_t ts)
{
        assert(!IS_TIMESTAMP(key->time) && in == GET_TXN(key->time));
        assert(HEK_STATE(out->end) == PREPARING);
        assert(HEK_STATE(in->end) >= PREPARING);
        bool ret;

        ret = false;
        lock(&in->latch);
        if (HEK_STATE(in->end) == PREPARING && in->end == ts) {
                key->next = in->dependents;
                in->dependents = key;
                out->must_wait = true;
                fetch_and_increment(&out->dep_count);
                ret = true;
        } else {
                /* in committed; it must be done with dependents */
                //                assert(in->dependents == NULL);
                ret = (HEK_STATE(in->end) == COMMIT) &&
                        (HEK_TIME(in->end) == HEK_TIME(ts));
        }
        unlock(&in->latch);
        return ret;
        /*
        } else if (HEK_STATE(in->end) == ABORT &&
                   cmp_and_swap(&out->dep_flag, PREPARING, ABORT)) {
                config.abort_queue->enqueue(out);
        } 
        unlock(&in->latch);
        */
        
}

bool hek_worker::validate_single(hek_action *txn, hek_key *key)
{
        assert(!IS_TIMESTAMP(txn->end) && HEK_STATE(txn->end) == PREPARING);
        struct hek_record *vis_record, *read_record;
        uint64_t vis_ts, read_ts, record_key, end_ts, vis_txn_ts, read_txn_ts;
        uint32_t table_id;

        if (SNAPSHOT_ISOLATION || txn->readonly == true)
                end_ts = HEK_TIME(txn->begin);
        else 
                end_ts = HEK_TIME(txn->end);
        table_id = key->table_id;
        record_key = key->key;
        read_record = key->value;
        read_ts = key->time;
        read_txn_ts = key->txn_ts;
        vis_record = config.tables[table_id]->get_version(record_key, end_ts,
                                                          &vis_ts, &vis_txn_ts);
        if (vis_record == read_record) {
                if (IS_TIMESTAMP(read_ts)) {
                        return read_ts == vis_ts;
                } else if (!IS_TIMESTAMP(vis_ts) &&
                           GET_TXN(vis_ts) == GET_TXN(read_ts) &&
                           HEK_TIME(read_txn_ts) == HEK_TIME(vis_txn_ts)) {
                        //                        return false;
                        key->txn = txn;
                        return add_commit_dep(txn, key, GET_TXN(vis_ts),
                                              vis_txn_ts);
                } else if (IS_TIMESTAMP(vis_ts)) {
                        return HEK_TIME(read_txn_ts) == vis_ts;
                }
        }
        return false;
}

hek_record* hek_worker::get_new_record(uint32_t table_id)
{
        hek_record *ret;

        ret = records[table_id];
        assert(ret != NULL);
        records[table_id] = ret->next;
        ret->next = NULL;
        return ret;       
}

/*
void hek_worker::return_record(uint32_t table_id, hek_record *record)
{
        //        memset(record, 0x0, sizeof(hek_record));
        record->next = records[table_id];
        records[table_id] = record;
}
*/

bool hek_worker::validate_reads(hek_action *txn)
{
        assert(!IS_TIMESTAMP(txn->end));
        assert(HEK_STATE(txn->end) == PREPARING);
        uint32_t num_reads, i;
        
        barrier();
        txn->must_wait = false;
        txn->dep_flag = PREPARING;
        txn->dep_count = 0;
        barrier();
        fetch_and_increment(&txn->dep_count);
        num_reads = txn->readset.size();
        for (i = 0; i < num_reads; ++i) {
                if (!validate_single(txn, &txn->readset[i])) {
                        if (cmp_and_swap((volatile uint64_t*)&txn->dep_flag,
                                         PREPARING,
                                         ABORT)) {
                                return false;
                        } else {
                                assert(txn->must_wait == true &&
                                       txn->dep_flag == ABORT);
                                return true;
                        }                        
                }
        }
        if (fetch_and_decrement(&txn->dep_count) == 0) {
                assert(txn->dep_flag == PREPARING);
                //                old_flag = xchgq((volatile uint64_t*)&txn->dep_flag,
                //                                 COMMIT);
                //                assert(old_flag == PREPARING);
                //                assert(HEK_STATE(txn->end) == PREPARING);
                txn->must_wait = false;
        }
        return true;
}

/*
void hek_worker::install_writes(hek_action *txn)
{
        uint32_t num_writes, i, table_id;
        uint64_t key;
        void *prev_ptr, *cur_ptr;
        for (i = 0; i < num_writes; ++i) {
                xchgq(END_TS_FIELD(prev_ptr), txn->end);
                xchgq(BEGIN_TS_FIELD(cur_ptr), txn->end);
        }
}
*/

/* 
 * Insert records written by a transaction. If all insertions succeed, it does 
 * not mean that the txn will commit. Reads must still be validated, and writes 
 * subsequently finalized.      
 */
bool hek_worker::insert_writes(hek_action *txn)
{

        uint32_t num_writes, i, tbl_id;
        hek_table *table;
        hek_record *rec;

        num_writes = txn->writeset.size();
        for (i = 0; i < num_writes; ++i) {
                assert(txn->writeset[i].written == false);
                assert(IS_TIMESTAMP((uint64_t)txn));  //ptr must be aligned 
                rec = txn->writeset[i].value;
                rec->begin = (uint64_t)txn | 0x1;
                rec->end = HEK_INF;
                tbl_id = txn->writeset[i].table_id;
                table = config.tables[tbl_id];
                if (!table->insert_version(rec, txn->begin)) 
                        return false;                
                else
                        txn->writeset[i].written = true;
        }

        return true;
}

/*
void hek_worker::run_readonly(hek_action *txn)
{
        assert(txn->is_readonly == true);
        volatile uint64_t cur_time;
}
*/

// 1. Run txn logic (may abort due to write-write conflicts)
// 2. Validate reads
// 3. Check if the txn depends on others. If yes, wait for commit dependencies,
// otherwise, abort.
// 
void hek_worker::run_txn(hek_action *txn)
{
        hek_status status;
        bool validated;

        txn->latch = 0;
        txn->worker = this;
        txn->dependents = NULL;
        barrier();
        get_writes(txn);
        while (true) {
                txn->begin = CREATE_EXEC_TIMESTAMP(get_timestamp());
        
                //                CREATE_EXEC_TIMESTAMP(fetch_and_increment(config.global_time));
                transition_begin(txn);
                get_reads(txn);
                status = txn->Run();
                transition_preparing(txn);
                if (!insert_writes(txn))
                        goto abort;
                //                if (!SNAPSHOT_ISOLATION)
                        validated = validate_reads(txn);
                        //                else
                        //                        validated = true;
                if (validated == true) {
                        if (txn->must_wait == false) {
                                transition_commit(txn);
                                //                        do_commit(txn);
                        }
                        return;
                } 
        abort:
                transition_abort(txn);

        }
        //        do_abort(txn);
}

void hek_worker::kill_waiters(hek_action *txn)
{
        assert(HEK_STATE(txn->end) == ABORT);
        assert(txn->latch == 1);
        hek_key **wait_record;
        hek_action *waiter;
        
        wait_record = &txn->dependents;
        while (*wait_record != NULL) {
                waiter = (*wait_record)->txn;
                assert(waiter->dep_count > 0);
                if (cmp_and_swap(&waiter->dep_flag, PREPARING, ABORT)) 
                        insert_abort_queue(waiter);
                wait_record = &((*wait_record)->next);
        }
        txn->dependents = NULL;
}

/*
 *  Decrease dependency count of each txn in the dependents list. 
 */
void hek_worker::commit_waiters(hek_action *txn)
{
        assert(HEK_STATE(txn->end) == COMMIT);
        assert(txn->latch == 1);
        hek_key **wait_record;
        hek_action *waiter;
        uint64_t flag;

        wait_record = &txn->dependents;
        while (*wait_record != NULL) {
                waiter = (*wait_record)->txn;                
                if (fetch_and_decrement(&waiter->dep_count) == 0) {
                        flag = xchgq((volatile uint64_t*)&waiter->dep_flag,
                                     (uint64_t)COMMIT);
                        assert(flag == PREPARING);
                        insert_commit_queue(waiter);
                }
                wait_record = &((*wait_record)->next);
        }
        txn->dependents = NULL;
}

void hek_worker::do_abort(hek_action *txn)
{
        assert(HEK_STATE(txn->end) == ABORT);
        remove_writes(txn);
        kill_waiters(txn);
        //        num_done += 1;        
}

void hek_worker::do_commit(hek_action *txn)
{
        assert(HEK_STATE(txn->end) == COMMIT);
        install_writes(txn);
        commit_waiters(txn);
        num_committed += 1;
        num_done += 1;
}

void hek_worker::install_writes(hek_action *txn)
{

        uint32_t num_writes, i;
        hek_key *key;
        //        uint64_t prev_ts;
        num_writes = txn->writeset.size();
        for (i = 0; i < num_writes; ++i) {
                key = &txn->writeset[i];
                assert(key->written == true);
                assert(key->value != NULL);
                assert(key->table_id < config.num_tables);
                assert(GET_TXN(key->value->begin) == txn);
                config.tables[key->table_id]->
                        finalize_version(key->value, HEK_TIME(txn->end));
        }

        
}

/*
 * If a transaction aborts, remove any versions it may have inserted into the 
 * table.
 */
void hek_worker::remove_writes(hek_action *txn)
{

        uint32_t num_writes, i, table_id;
        hek_key *key;
        hek_record *record;
        num_writes = txn->writeset.size();
        for (i = 0; i < num_writes; ++i) {
                if (txn->writeset[i].written == true) {
                        txn->writeset[i].written = false;
                        key = &txn->writeset[i];
                        record = key->value;
                        table_id = key->table_id;
                        assert(record != NULL);
                        assert(table_id < config.num_tables);
                        record = key->value;
                        config.tables[table_id]->remove_version(record);
                        //                        return_record(table_id, record);
                                                       
                } else {
                        break;
                }                
        }
        
}
