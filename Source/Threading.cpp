/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Threading.h"

namespace vl
{
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
