#ifndef SETUP_OCC_H_
#define SETUP_OCC_H_

#define OCC_WAIT_INTERVAL 1000
#define OCC_TXN_BUFFER 5
#define OCC_LOG_SIZE (((uint64_t)1)<<28)
#define OCC_EPOCH_SIZE 7984000

#include <occ_action.h>
#include <config.h>
#include <table.h>
#include <occ.h>
#include <record_generator.h>

struct occ_result {
        timespec time_elapsed;
        uint64_t num_txns;
};

OCCAction** create_single_occ_action_batch(uint32_t batch_size,
                                           OCCConfig config,
                                           RecordGenerator *gen);

OCCAction* generate_occ_rmw_action(OCCConfig config, RecordGenerator *gen);

OCCAction* generate_small_bank_occ_action(uint64_t numRecords, bool read_only);

OCCActionBatch** setup_occ_input(OCCConfig config, uint32_t iters);

OCCActionBatch* setup_occ_single_input(OCCConfig occ_config,
                                       workload_config w_conf);

OCCWorker** setup_occ_workers(SimpleQueue<OCCActionBatch> **inputQueue,
                              SimpleQueue<OCCActionBatch> **outputQueue,
                              Table **tables, int numThreads,
                              uint64_t epoch_threshold, uint32_t numTables);

void validate_ycsb_occ_tables(Table *table, uint64_t num_records);

Table** setup_ycsb_occ_tables(OCCConfig config);

Table** setup_small_bank_occ_tables(OCCConfig config);

Table** setup_occ_tables(OCCConfig config);

void write_occ_output(timespec elapsed_time, OCCConfig config);

struct occ_result do_measurement(SimpleQueue<OCCActionBatch> **inputQueues,
                                 SimpleQueue<OCCActionBatch> **outputQueues,
                                 OCCWorker **workers,
                                 OCCActionBatch **inputBatches,
                                 uint32_t num_batches,
                                 OCCConfig config,
                                 OCCActionBatch setup_txns,
                                 Table **tables,
                                 uint32_t num_tables);

struct occ_result run_occ_workers(SimpleQueue<OCCActionBatch> **inputQueues,
                                  SimpleQueue<OCCActionBatch> **outputQueues,
                                  OCCWorker **workers,
                                  OCCActionBatch *inputBatches,
                                  uint32_t num_batches,
                                  OCCConfig config,
                                  OCCActionBatch setup_txns);

void occ_experiment(OCCConfig config, workload_config conf);

#endif // SETUP_OCC_H_




        
        

