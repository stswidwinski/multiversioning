#ifndef HEK_ACTION_H_
#define HEK_ACTION_H_

#include <action.h>
#include <vector>
#include <db.h>

#define HEK_INF		0xFFFFFFFFFFFFFFF0
#define HEK_MASK	0x000000000000000F
#define EXECUTING 	0x0
#define PREPARING 	0x1
#define COMMIT		0x2
#define ABORT 		0x3

#define GET_TXN(ts) ((hek_action*)(HEK_INF & ts))
#define IS_TIMESTAMP(ts) ((ts & 0x0F) == 0)
#define HEK_TIME(ts) (HEK_INF & ts)
#define HEK_STATE(ts) (HEK_MASK & ts)
#define CREATE_EXEC_TIMESTAMP(ts) (ts<<8)
#define CREATE_PREP_TIMESTAMP(ts) ((ts<<8) | PREPARING)
//#define CREATE_COMMIT_TIMESTAMP(ts) ((ts<<8) | COMMIT)
//#define CREATE_ABORT_TIMESTAMP(ts) ((ts<<8) | ABORT)

class hek_action;
class hek_worker;
class hek_table;

struct hek_record {
        struct hek_record *next;
        uint64_t begin;
        uint64_t end;
        uint64_t key;
        uint32_t size;
        uint32_t writer;
        char value[0];
};

struct hek_status {
        bool validation;
        bool commit;
};

struct hek_key {
        uint64_t key;
        uint32_t table_id;
        hek_action *txn;		// Txn to which key belongs 
        hek_record *value;		// Ref to record (for reads)
        hek_key *next;			// To link commit deps
        hek_table *table_ptr;		// For reads, ptr to table
        uint64_t time;			// Timestamp of read record
        uint64_t prev_ts; 		// Prev version timestamp (for writes)
        bool written;			//
        bool is_rmw;
        uint64_t txn_ts;
};

// Align to 256 bytes because we use the least significant byte
// corresponding to the pointer.
class hek_action : public translator {
 public:
        std::vector<hek_key> readset;
        std::vector<hek_key> writeset;
        volatile uint64_t dep_flag;
        volatile uint64_t dep_count;
        volatile hek_action *next;
        hek_key *dependents;
        volatile uint64_t latch;
        uint64_t begin;
        uint64_t end;
        hek_worker *worker;
        bool must_wait;
        bool readonly;

 	hek_action(txn *t) : translator(t) {
                readonly = false;
        };
        
        virtual hek_status Run();
        virtual void* read(uint64_t key, uint32_t table_id);
        virtual void* write_ref(uint64_t key, uint32_t table_id);
        virtual int rand();
        
} __attribute__((__aligned__(256)));;
/*
class hek_rmw_action : public hek_action {
 public:
        virtual hek_status Run();
} __attribute__((__packed__, __aligned__(256)));

class hek_readonly_action : public hek_action {
        volatile char reads[1000];
 public:
        hek_readonly_action();
        virtual hek_status Run();
} __attribute__((__packed__, __aligned__(256)));

namespace hek_small_bank {
        class balance : public hek_action {
        private:
                volatile long total_balance;
                char *meta_data;
        public:
                balance(uint64_t customer_id, char *meta_data);
                virtual hek_status Run();                
        } __attribute__((__packed__, __aligned__(256)));

        class deposit_checking : public hek_action {
        private:
                long amount;
                char *meta_data;
        public:
                deposit_checking(uint64_t customer_id, long amount,
                                 char *meta_data);
                virtual hek_status Run();                
        } __attribute__((__packed__, __aligned__(256)));

        class transact_saving : public hek_action {
        private:
                long amount;
                char *meta_data;
        public:
                transact_saving(uint64_t cusomter_id, long amount,
                                char *meta_data);
                virtual hek_status Run();                
        } __attribute__((__packed__, __aligned__(256)));

        class amalgamate : public hek_action {
        private:
                char *meta_data;
        public:
                amalgamate(uint64_t from_customer, uint64_t to_customer,
                           char *meta_data);
                virtual hek_status Run();
                
        } __attribute__((__packed__, __aligned__(256)));

        class write_check : public hek_action {
        private:
                long amount;
                char *meta_data;
        public:
                write_check(uint64_t customer, long amount, char *meta_data);
                virtual hek_status Run();
        } __attribute__((__packed__, __aligned__(256)));
};
*/

#endif // HEK_ACTION_H_
