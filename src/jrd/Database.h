/*
 *      PROGRAM:        JRD access method
 *      MODULE:         Database.h
 *      DESCRIPTION:    Common descriptions
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 *
 * 2002.10.28 Sean Leyne - Code cleanup, removed obsolete "DecOSF" port
 *
 * 2002.10.29 Sean Leyne - Removed obsolete "Netware" port
 * Claudio Valderrama C.
 *
 */

#ifndef JRD_DATABASE_H
#define JRD_DATABASE_H

#include "firebird.h"
#include "../jrd/cch.h"
#include "../common/gdsassert.h"
#include "../common/common.h"
#include "../common/dsc.h"
#include "../jrd/btn.h"
#include "../jrd/jrd_proto.h"
#include "../jrd/val.h"
#include "../jrd/irq.h"
#include "../jrd/drq.h"
#include "../include/gen/iberror.h"

#include "../common/classes/fb_atomic.h"
#include "../common/classes/fb_string.h"
#include "../common/classes/MetaName.h"
#include "../common/classes/array.h"
#include "../common/classes/objects_array.h"
#include "../common/classes/stack.h"
#include "../common/classes/timestamp.h"
#include "../common/classes/GenericMap.h"
#include "../common/classes/RefCounted.h"
#include "../common/classes/semaphore.h"
#include "../common/utils_proto.h"
#include "../jrd/RandomGenerator.h"
#include "../common/os/guid.h"
#include "../jrd/sbm.h"
#include "../jrd/flu.h"
#include "../jrd/RuntimeStatistics.h"
#include "../jrd/os/thd_priority.h"
#include "../jrd/event_proto.h"
#include "../lock/lock_proto.h"
#include "../common/config/config.h"
#include "../common/classes/SyncObject.h"
#include "../common/classes/Synchronize.h"

namespace Jrd
{
template <typename T> class vec;
class jrd_rel;
class Shadow;
class BlobFilter;
class TipCache;
class BackupManager;
class ExternalFileDirectoryList;
class MonitoringData;


// general purpose vector
template <class T, BlockType TYPE = type_vec>
class vec_base : protected pool_alloc<TYPE>
{
public:
	typedef typename Firebird::Array<T>::iterator iterator;
	typedef typename Firebird::Array<T>::const_iterator const_iterator;

	/*
	static vec_base* newVector(MemoryPool& p, int len)
	{
		return FB_NEW(p) vec_base<T, TYPE>(p, len);
	}

	static vec_base* newVector(MemoryPool& p, const vec_base& base)
	{
		return FB_NEW(p) vec_base<T, TYPE>(p, base);
	}
	*/

	size_t count() const { return v.getCount(); }
	T& operator[](size_t index) { return v[index]; }
	const T& operator[](size_t index) const { return v[index]; }

	iterator begin() { return v.begin(); }
	iterator end() { return v.end(); }

	const_iterator begin() const { return v.begin(); }
	const_iterator end() const { return v.end(); }

	void clear() { v.clear(); }

	T* memPtr() { return &v[0]; }

	void resize(size_t n, T val = T()) { v.resize(n, val); }

	void operator delete(void* mem) { MemoryPool::globalFree(mem); }

protected:
	vec_base(MemoryPool& p, int len)
		: v(p, len)
	{
		v.resize(len);
	}

	vec_base(MemoryPool& p, const vec_base& base)
		: v(p)
	{
		v = base.v;
	}

private:
	Firebird::Array<T> v;
};

template <typename T>
class vec : public vec_base<T, type_vec>
{
public:
	static vec* newVector(MemoryPool& p, int len)
	{
		return FB_NEW(p) vec<T>(p, len);
	}

	static vec* newVector(MemoryPool& p, const vec& base)
	{
		return FB_NEW(p) vec<T>(p, base);
	}

	static vec* newVector(MemoryPool& p, vec* base, int len)
	{
		if (!base)
			base = FB_NEW(p) vec<T>(p, len);
		else if (len > (int) base->count())
			base->resize(len);
		return base;
	}

private:
	vec(MemoryPool& p, int len) : vec_base<T, type_vec>(p, len) {}
	vec(MemoryPool& p, const vec& base) : vec_base<T, type_vec>(p, base) {}
};

class vcl : public vec_base<ULONG, type_vcl>
{
public:
	static vcl* newVector(MemoryPool& p, int len)
	{
		return FB_NEW(p) vcl(p, len);
	}

	static vcl* newVector(MemoryPool& p, const vcl& base)
	{
		return FB_NEW(p) vcl(p, base);
	}

	static vcl* newVector(MemoryPool& p, vcl* base, int len)
	{
		if (!base)
			base = FB_NEW(p) vcl(p, len);
		else if (len > (int) base->count())
			base->resize(len);
		return base;
	}

private:
	vcl(MemoryPool& p, int len) : vec_base<ULONG, type_vcl>(p, len) {}
	vcl(MemoryPool& p, const vcl& base) : vec_base<ULONG, type_vcl>(p, base) {}
};

typedef vec<SLONG> TransactionsVector;


//
// bit values for dbb_flags
//
const ULONG DBB_damaged				= 0x1L;
const ULONG DBB_exclusive			= 0x2L;		// Database is accessed in exclusive mode
const ULONG DBB_bugcheck			= 0x4L;		// Bugcheck has occurred
#ifdef GARBAGE_THREAD
const ULONG DBB_garbage_collector	= 0x8L;		// garbage collector thread exists
const ULONG DBB_gc_active			= 0x10L;	// ... and is actively working.
const ULONG DBB_gc_pending			= 0x20L;	// garbage collection requested
#endif
const ULONG DBB_force_write			= 0x40L;	// Database is forced write
const ULONG DBB_no_reserve			= 0x80L;	// No reserve space for versions
const ULONG DBB_DB_SQL_dialect_3	= 0x100L;	// database SQL dialect 3
const ULONG DBB_read_only			= 0x200L;	// DB is ReadOnly (RO). If not set, DB is RW
const ULONG DBB_being_opened_read_only	= 0x400L;	// DB is being opened RO. If unset, opened as RW
const ULONG DBB_not_in_use			= 0x800L;	// Database to be ignored while attaching
const ULONG DBB_lck_init_done		= 0x1000L;	// LCK_init() called for the database
const ULONG DBB_sweep_in_progress	= 0x2000L;	// A database sweep operation is in progress
const ULONG DBB_security_db			= 0x4000L;	// ISC security database
const ULONG DBB_suspend_bgio		= 0x8000L;	// Suspend I/O by background threads
const ULONG DBB_being_opened		= 0x10000L;	// database is being attached to
const ULONG DBB_gc_cooperative		= 0x20000L;	// cooperative garbage collection
const ULONG DBB_gc_background		= 0x40000L;	// background garbage collection by gc_thread
const ULONG DBB_no_fs_cache			= 0x80000L;	// Not using file system cache
const ULONG DBB_destroying			= 0x100000L;	// database destructor is called

//
// dbb_ast_flags
//
const ULONG DBB_blocking			= 0x1L;		// Exclusive mode is blocking
const ULONG DBB_get_shadows			= 0x2L;		// Signal received to check for new shadows
const ULONG DBB_assert_locks		= 0x4L;		// Locks are to be asserted
const ULONG DBB_shutdown			= 0x8L;		// Database is shutdown
const ULONG DBB_shut_attach			= 0x10L;	// no new attachments accepted
const ULONG DBB_shut_tran			= 0x20L;	// no new transactions accepted
const ULONG DBB_shut_force			= 0x40L;	// forced shutdown in progress
const ULONG DBB_shutdown_locks		= 0x80L;	// Database locks release by shutdown
const ULONG DBB_shutdown_full		= 0x100L;	// Database fully shut down
const ULONG DBB_shutdown_single		= 0x200L;	// Database is in single-user maintenance mode
const ULONG DBB_monitor_off			= 0x400L;	// Database has the monitoring lock released

class Database : public pool_alloc<type_dbb>
{
public:
	class SharedCounter
	{
		static const ULONG DEFAULT_CACHE_SIZE = 16;

		struct ValueCache
		{
			Lock* lock;		// lock which holds shared counter value
			SLONG curVal;	// current value of shared counter lock
			SLONG maxVal;	// maximum cached value of shared counter lock
		};

	public:
		enum
		{
			ATTACHMENT_ID_SPACE = 0,
			TRANSACTION_ID_SPACE = 1,
			STATEMENT_ID_SPACE = 2,
			TOTAL_ITEMS = 3
		};

		SharedCounter();
		~SharedCounter();

		SLONG generate(thread_db* tdbb, ULONG space, ULONG prefetch = DEFAULT_CACHE_SIZE);
		void shutdown(thread_db* tdbb);

	private:
		static int blockingAst(void* arg);

		ValueCache m_counters[TOTAL_ITEMS];
	};

	typedef int (*crypt_routine) (const char*, void*, int, void*);

	static Database* create()
	{
		Firebird::MemoryStats temp_stats;
		MemoryPool* const pool = MemoryPool::createPool(NULL, temp_stats);
		Database* const dbb = FB_NEW(*pool) Database(pool);
		pool->setStatsGroup(dbb->dbb_memory_stats);
		return dbb;
	}

	// The destroy() function MUST be used to delete a Database object.
	// The function hides some tricky order of operations.  Since the
	// memory for the vectors in the Database is allocated out of the Database's
	// permanent memory pool, the entire delete() operation needs
	// to complete _before_ the permanent pool is deleted, or else
	// risk an aborted engine.
	static void destroy(Database* const toDelete)
	{
		if (!toDelete)
			return;

		MemoryPool* const perm = toDelete->dbb_permanent;

		// Memory pool destruction below decrements memory statistics
		// situated in database block we are about to deallocate right now
		Firebird::MemoryStats temp_stats;
		perm->setStatsGroup(temp_stats);

		delete toDelete;
		MemoryPool::deletePool(perm);
	}

	static ULONG getLockOwnerId()
	{
		return fb_utils::genUniqueId();
	}

	Firebird::SyncObject	dbb_sync;
	Firebird::SyncObject	dbb_lck_sync;		// syncronize operations with att_long_locks at different attachments


	LockManager*	dbb_lock_mgr;
	EventManager*	dbb_event_mgr;

	Database*	dbb_next;				// Next database block in system
	Attachment* dbb_attachments;		// Active attachments
	BufferControl*	dbb_bcb;			// Buffer control block
	int			dbb_monitoring_id;		// dbb monitoring identifier
	Lock* 		dbb_lock;				// granddaddy lock
	
	Firebird::SyncObject	dbb_sh_counter_sync;
	
	Firebird::SyncObject	dbb_shadow_sync;
	Shadow*		dbb_shadow;				// shadow control block
	Lock*		dbb_shadow_lock;		// lock for synchronizing addition of shadows

	Lock*		dbb_retaining_lock;		// lock for preserving commit retaining snapshot
	Lock*		dbb_monitor_lock;		// lock for monitoring purposes
	PageManager dbb_page_manager;
	vcl*		dbb_t_pages;			// pages number for transactions
	vcl*		dbb_gen_id_pages;		// known pages for gen_id
	BlobFilter*	dbb_blob_filters;		// known blob filters

	Firebird::SyncObject	dbb_mon_sync;			// syncronize operations with dbb_monitor_lock
	MonitoringData*			dbb_monitoring_data;	// monitoring data

	DatabaseModules	dbb_modules;		// external function/filter modules
	ExtEngineManager dbb_extManager;	// external engine manager

	Firebird::SyncObject	dbb_flush_count_mutex;

	Firebird::AtomicCounter dbb_ast_flags;		// flags modified at AST level
	Firebird::AtomicCounter dbb_flags;
	USHORT dbb_ods_version;				// major ODS version number
	USHORT dbb_minor_version;			// minor ODS version number
	USHORT dbb_page_size;				// page size
	USHORT dbb_dp_per_pp;				// data pages per pointer page
	USHORT dbb_max_records;				// max record per data page
	USHORT dbb_max_idx;					// max number of indexes on a root page
	USHORT dbb_use_count;				// active count of threads

#ifdef SUPERSERVER_V2
	USHORT dbb_prefetch_sequence;		// sequence to pace frequency of prefetch requests
	USHORT dbb_prefetch_pages;			// prefetch pages per request
#endif

	Firebird::PathName dbb_filename;	// filename string
	Firebird::PathName dbb_database_name;	// database ID (file name or alias)
	Firebird::string dbb_encrypt_key;	// encryption key

	MemoryPool* dbb_permanent;

	Firebird::SyncObject			dbb_pools_sync;
	Firebird::Array<MemoryPool*>	dbb_pools;		// pools

	SLONG dbb_oldest_active;			// Cached "oldest active" transaction
	SLONG dbb_oldest_transaction;		// Cached "oldest interesting" transaction
	SLONG dbb_oldest_snapshot;			// Cached "oldest snapshot" of all active transactions
	SLONG dbb_next_transaction;			// Next transaction id used by NETWARE
	SLONG dbb_attachment_id;			// Next attachment id for ReadOnly DB's
	ULONG dbb_page_buffers;				// Page buffers from header page


#ifdef GARBAGE_THREAD
	Firebird::Semaphore dbb_gc_sem;		// Event to wake up garbage collector
	Firebird::Semaphore dbb_gc_init;	// Event for initialization garbage collector
	Firebird::Semaphore dbb_gc_fini;	// Event for finalization garbage collector
#endif

	Firebird::MemoryStats dbb_memory_stats;

	RuntimeStatistics dbb_stats;
	SLONG dbb_last_header_write;		// Transaction id of last header page physical write
	SLONG dbb_flush_cycle;				// Current flush cycle
	SLONG dbb_sweep_interval;			// Transactions between sweep
	const ULONG dbb_lock_owner_id;		// ID for the lock manager
	SLONG dbb_lock_owner_handle;		// Handle for the lock manager

	USHORT unflushed_writes;			// unflushed writes
	time_t last_flushed_write;			// last flushed write time

	crypt_routine dbb_encrypt;			// External encryption routine
	crypt_routine dbb_decrypt;			// External decryption routine

	TipCache*		dbb_tip_cache;		// cache of latest known state of all transactions in system
	TransactionsVector*	dbb_pc_transactions;				// active precommitted transactions
	BackupManager*	dbb_backup_manager;						// physical backup manager
	Firebird::TimeStamp dbb_creation_date; 					// creation date
	ExternalFileDirectoryList* dbb_external_file_directory_list;
	Firebird::RefPtr<Config> dbb_config;

	SharedCounter dbb_shared_counter;

	// returns true if primary file is located on raw device
	bool onRawDevice() const;

	// returns an unique ID string for a database file
	Firebird::string getUniqueFileId() const;

	MemoryPool* createPool()
	{
		MemoryPool* const pool = MemoryPool::createPool(dbb_permanent, dbb_memory_stats);

		Firebird::SyncLockGuard guard(&dbb_pools_sync, Firebird::SYNC_EXCLUSIVE, "Database::createPool");
		dbb_pools.add(pool);
		return pool;
	}

	void deletePool(MemoryPool* pool);

private:
	explicit Database(MemoryPool* p)
	:	dbb_page_manager(this, *p),
		dbb_modules(*p),
		dbb_extManager(*p),
		dbb_filename(*p),
		dbb_database_name(*p),
		dbb_encrypt_key(*p),
		dbb_permanent(p),
		dbb_pools(*p, 4),
		dbb_stats(*p),
		dbb_lock_owner_id(getLockOwnerId()),
		dbb_creation_date(Firebird::TimeStamp::getCurrentTimeStamp()),
		dbb_tip_cache(NULL),
		dbb_external_file_directory_list(NULL)
	{
		dbb_pools.add(p);
	}

	~Database();

public:
	SLONG generateAttachmentId(thread_db* tdbb)
	{
		return dbb_shared_counter.generate(tdbb, SharedCounter::ATTACHMENT_ID_SPACE, 1);
	}

	SLONG generateTransactionId(thread_db* tdbb)
	{
		return dbb_shared_counter.generate(tdbb, SharedCounter::TRANSACTION_ID_SPACE, 1);
	}

	SLONG generateStatementId(thread_db* tdbb)
	{
		return dbb_shared_counter.generate(tdbb, SharedCounter::STATEMENT_ID_SPACE);
	}

	USHORT getMaxIndexKeyLength() const
	{
		return dbb_page_size / 4;
	}

private:
	static int blockingAstSharedCounter(void*);

	// The delete operators are no-oped because the Database memory is allocated from the
	// Database's own permanent pool.  That pool has already been released by the Database
	// destructor, so the memory has already been released.  Hence the operator
	// delete no-op.
	void operator delete(void*) {}
	void operator delete[](void*) {}

	Database(const Database&);			// no impl.
	const Database& operator =(const Database&) { return *this; }
};

} // namespace Jrd

#endif // JRD_DATABASE_H
