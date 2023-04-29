/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Threading.h"

#if defined VCZH_ARM
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
		vint expected = 0;
		while (!token.compare_exchange_strong(expected, 1))
		{
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
}
