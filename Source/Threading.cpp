/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Threading.h"

#if defined VCZH_ARM
#include <arm_acle.h>
#elif defined VCZH_MSVC || defined VCZH_GCC
#include <emmintrin.h>
#endif

namespace vl
{

/***********************************************************************
SpinLock
***********************************************************************/

	SpinLock::Scope::Scope(SpinLock& _spinLock)
		:spinLock(&_spinLock)
	{
		spinLock->Enter();
	}

	SpinLock::Scope::~Scope()
	{
		spinLock->Leave();
	}

	bool SpinLock::TryEnter()
	{
		return token.exchange(1) == 0;
	}

	void SpinLock::Enter()
	{
		while (true)
		{
			vint expected = 0;
			if (token.compare_exchange_strong(expected, 1))
			{
				return;
			}
			while (token != 0)
			{
#ifdef VCZH_ARM
				__yield();
#else
				_mm_pause();
#endif
			}
		}
	}

	void SpinLock::Leave()
	{
		token.exchange(0);
	}

/***********************************************************************
ThreadLocalStorage
***********************************************************************/

	void ThreadLocalStorage::Clear()
	{
		CHECK_ERROR(!disposed, L"vl::ThreadLocalStorage::Clear()#Cannot access a disposed ThreadLocalStorage.");
		if(destructor)
		{
			if (auto data = Get())
			{
				destructor(data);
			}
		}
		Set(nullptr);
	}

	void ThreadLocalStorage::Dispose()
	{
		CHECK_ERROR(!disposed, L"vl::ThreadLocalStorage::Dispose()#Cannot access a disposed ThreadLocalStorage.");
		Clear();
		disposed = true;
	}

	struct TlsStorageLink
	{
		ThreadLocalStorage*		storage = nullptr;
		TlsStorageLink*			next = nullptr;
	};

	volatile bool				tlsFixed = false;
	TlsStorageLink*				tlsHead = nullptr;
	TlsStorageLink**			tlsTail = &tlsHead;

	void ThreadLocalStorage::PushStorage(ThreadLocalStorage* storage)
	{
		CHECK_ERROR(!tlsFixed, L"vl::ThreadLocalStorage::PushStorage(ThreadLocalStorage*)#Cannot create new ThreadLocalStorage instance after calling ThreadLocalStorage::FixStorages().");
		auto link = new TlsStorageLink;
		link->storage = storage;
		*tlsTail = link;
		tlsTail = &link->next;
	}

	void ThreadLocalStorage::FixStorages()
	{
		tlsFixed = true;
	}

	void ThreadLocalStorage::ClearStorages()
	{
		FixStorages();
		auto current = tlsHead;
		while (current)
		{
			current->storage->Clear();
			current = current->next;
		}
	}

	void ThreadLocalStorage::DisposeStorages()
	{
		FixStorages();
		auto current = tlsHead;
		tlsHead = nullptr;
		tlsTail = nullptr;
		while (current)
		{
			current->storage->Dispose();

			auto temp = current;
			current = current->next;
			delete temp;
		}
	}

/***********************************************************************
TaskQueue
***********************************************************************/

	TaskQueue::TaskQueue()
	{
		CHECK_ERROR(semaphoreTasks.Create(0, 65536), L"vl::TaskQueue::TaskQueue()#Failed to create the task semaphore.");
	}

	TaskQueue::~TaskQueue()
	{
	}

	void TaskQueue::QueueTask(Func<void()> task)
	{
		SPIN_LOCK(lockTasks)
		{
			tasks.Add(task);
		}
		semaphoreTasks.Release();
	}

	void TaskQueue::QueueExitTask()
	{
		SPIN_LOCK(lockTasks)
		{
			exitTaskQueued = true;
		}
		semaphoreTasks.Release();
	}

	void TaskQueue::RunTaskQueue()
	{
		while (true)
		{
			Func<void()> task;
			bool hasTask = false;
			bool shouldExit = false;
			SPIN_LOCK(lockTasks)
			{
				if (tasks.Count() > 0)
				{
					task = tasks[0];
					tasks.RemoveAt(0);
					hasTask = true;
				}
				else
				{
					shouldExit = exitTaskQueued;
				}
			}

			if (shouldExit)
			{
				break;
			}
			if (!hasTask)
			{
				semaphoreTasks.Wait();
				continue;
			}
			task();
		}
	}
}
