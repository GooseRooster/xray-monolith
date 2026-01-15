#pragma once

#include <mutex>
#include <shared_mutex>
#include "_noncopyable.h"
#if 0//def DEBUG
# define PROFILE_CRITICAL_SECTIONS
#endif // DEBUG

#ifdef PROFILE_CRITICAL_SECTIONS
typedef void(*add_profile_portion_callback) (LPCSTR id, const u64& time);
void XRCORE_API set_add_profile_portion(add_profile_portion_callback callback);

# define STRINGIZER_HELPER(a) #a
# define STRINGIZER(a) STRINGIZER_HELPER(a)
# define CONCATENIZE_HELPER(a,b) a##b
# define CONCATENIZE(a,b) CONCATENIZE_HELPER(a,b)
# define MUTEX_PROFILE_PREFIX_ID #mutexes/
# define MUTEX_PROFILE_ID(a) STRINGIZER(CONCATENIZE(MUTEX_PROFILE_PREFIX_ID,a))
#endif // PROFILE_CRITICAL_SECTIONS

// Desc: Simple wrapper for critical section
class XRCORE_API xrCriticalSection : xray::noncopyable
{
public:
	class XRCORE_API raii
	{
	public:
		raii(xrCriticalSection*);
		~raii();

	private:
		xrCriticalSection* critical_section;
	};

private:
	void* pmutex;
#ifdef PROFILE_CRITICAL_SECTIONS
    LPCSTR m_id;
#endif // PROFILE_CRITICAL_SECTIONS

public:
#ifdef PROFILE_CRITICAL_SECTIONS
    xrCriticalSection(LPCSTR id);
#else // PROFILE_CRITICAL_SECTIONS
	xrCriticalSection();
#endif // PROFILE_CRITICAL_SECTIONS
	~xrCriticalSection();

	void Enter();
	void Leave();
	BOOL TryEnter();

	bool IsValid() { return pmutex != nullptr; }
};

class xrCriticalSectionGuard : xray::noncopyable
{
private:
	xrCriticalSection* critical_section;

public:
	void Enter()
	{
		critical_section->Enter();
	}
	void Leave()
	{
		critical_section->Leave();
	}
	xrCriticalSectionGuard(xrCriticalSection* cs) : critical_section(cs) { Enter(); }
	xrCriticalSectionGuard(xrCriticalSection& cs) : critical_section(&cs) { Enter(); }

	~xrCriticalSectionGuard() { Leave(); }
};

// Slim Reader/Writer Lock - high-performance lock for read-heavy workloads
// Multiple readers can hold the lock simultaneously, but writers get exclusive access
// Use AcquireShared/ReleaseShared for read operations
// Use AcquireExclusive/ReleaseExclusive for write operations
class XRCORE_API xrSRWLock : xray::noncopyable
{
private:
	SRWLOCK m_lock;

public:
	xrSRWLock();
	~xrSRWLock();

	// Exclusive (write) access - blocks all other readers and writers
	void AcquireExclusive();
	void ReleaseExclusive();
	BOOL TryAcquireExclusive();

	// Shared (read) access - allows multiple concurrent readers
	void AcquireShared();
	void ReleaseShared();
	BOOL TryAcquireShared();
};

// RAII guard for xrSRWLock
// Usage for writes: xrSRWLockGuard guard(lock);           // exclusive by default
// Usage for reads:  xrSRWLockGuard guard(lock, true);     // shared access
class xrSRWLockGuard : xray::noncopyable
{
private:
	xrSRWLock* m_lock;
	bool m_shared;

public:
	xrSRWLockGuard(xrSRWLock& lock, bool shared = false);
	xrSRWLockGuard(xrSRWLock* lock, bool shared = false);
	~xrSRWLockGuard();
};

