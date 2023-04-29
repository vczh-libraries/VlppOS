#include "../../Source/Threading.h"

using namespace vl;
using namespace vl::collections;

namespace mynamespace
{
	/***********************************************************************
	Thread
	***********************************************************************/

	struct ThreadData
	{
		bool modified;

		ThreadData()
			:modified(false)
		{
		}
	};

	void SimpleThreadProc(Thread* thread, void* argument)
	{
		Thread::Sleep(1000);
		((ThreadData*)argument)->modified=true;
	}

	/***********************************************************************
	Mutex
	***********************************************************************/

	struct Mutex_ThreadData
	{
		Mutex				mutex;
		atomic_vint			counter;

		Mutex_ThreadData()
			:counter(0)
		{
			TEST_ASSERT(mutex.Create(true));
		}
	};

	void Mutex_ThreadProc(Thread* thread, void* argument)
	{
		Mutex_ThreadData* data = (Mutex_ThreadData*)argument;
		{
			TEST_ASSERT(data->mutex.Wait());
			INCRC(&data->counter);
			TEST_ASSERT(data->mutex.Release());
		}
	}

	/***********************************************************************
	Semaphore
	***********************************************************************/

	struct Semaphore_ThreadData
	{
		CriticalSection		cs;
		Semaphore			semaphore;
		atomic_vint			counter;

		Semaphore_ThreadData(vint max)
			:counter(0)
		{
			TEST_ASSERT(semaphore.Create(0, max));
		}
	};

	void Semaphore_ThreadProc(Thread* thread, void* argument)
	{
		Semaphore_ThreadData* data = (Semaphore_ThreadData*)argument;
		TEST_ASSERT(data->semaphore.Wait());
		{
			CriticalSection::Scope lock(data->cs);
			INCRC(&data->counter);
		}
	}

	/***********************************************************************
	EventObject
	***********************************************************************/

	struct ManualEvent_ThreadData
	{
		CriticalSection		cs;
		EventObject			eventObject;
		atomic_vint			counter;

		ManualEvent_ThreadData()
			:counter(0)
		{
			TEST_ASSERT(eventObject.CreateManualUnsignal(false));
		}
	};

	void ManualEvent_ThreadProc(Thread* thread, void* argument)
	{
		ManualEvent_ThreadData* data = (ManualEvent_ThreadData*)argument;
		TEST_ASSERT(data->eventObject.Wait());
		{
			CriticalSection::Scope lock(data->cs);
			INCRC(&data->counter);
		}
	}

	struct AutoEvent_ThreadData
	{
		EventObject			eventObject;
		atomic_vint			counter;

		AutoEvent_ThreadData()
			:counter(0)
		{
			TEST_ASSERT(eventObject.CreateAutoUnsignal(false));
		}
	};

	void AutoEvent_ThreadProc(Thread* thread, void* argument)
	{
		AutoEvent_ThreadData* data = (AutoEvent_ThreadData*)argument;
		TEST_ASSERT(data->eventObject.Wait());
		INCRC(&data->counter);
	}

	/***********************************************************************
	CriticalSection
	***********************************************************************/

	struct CS_ThreadData
	{
		CriticalSection			cs;
		atomic_vint				counter;

		CS_ThreadData()
			:counter(0)
		{
		}
	};

	void CS_ThreadProc(Thread* thread, void* argument)
	{
		CS_ThreadData* data = (CS_ThreadData*)argument;
		{
			CriticalSection::Scope lock(data->cs);
			INCRC(&data->counter);
		}
	}

	/***********************************************************************
	ReaderWriterLock
	***********************************************************************/

	struct SRW_ThreadData
	{
		EventObject				ev;
		SpinLock				sl;
		ReaderWriterLock		lock;
		atomic_vint				counter;

		SRW_ThreadData()
			:counter(0)
		{
			ev.CreateManualUnsignal(false);
		}
	};

	void SRW_ReaderProc(Thread* thread, void* argument)
	{
		SRW_ThreadData* data = (SRW_ThreadData*)argument;
		data->ev.Wait();
		for (vint i = 0; i < 10; i++)
		{
			ReaderWriterLock::ReaderScope srw(data->lock);
			SpinLock::Scope sl(data->sl);
			INCRC(&data->counter);
		}
	}

	void SRW_WriterProc(Thread* thread, void* argument)
	{
		SRW_ThreadData* data = (SRW_ThreadData*)argument;
		data->ev.Wait();
		for (vint i = 0; i < 10; i++)
		{
			ReaderWriterLock::WriterScope srw(data->lock);
			SpinLock::Scope sl(data->sl);
			INCRC(&data->counter);
		}
	}

	/***********************************************************************
	SpinLock
	***********************************************************************/

	struct SL_ThreadData
	{
		SpinLock				lock;
		atomic_vint				counter;

		SL_ThreadData()
			:counter(0)
		{
		}
	};

	void SL_ThreadProc(Thread* thread, void* argument)
	{
		SL_ThreadData* data = (SL_ThreadData*)argument;
		{
			SpinLock::Scope lock(data->lock);
			INCRC(&data->counter);
		}
	}

	/***********************************************************************
	Thread Local Storage
	***********************************************************************/

	ThreadVariable<int> tls1;
	ThreadVariable<const wchar_t*> tls2;
	ThreadVariable<WString> tls3;

	void TlsProc(int i, atomic_vint& counter)
	{
		TEST_ASSERT(tls1.HasData() == false);
		tls1.Set(i);
		TEST_ASSERT(tls1.HasData() == true);
		TEST_ASSERT(tls1.Get() == i);
		tls1.Clear();
		TEST_ASSERT(tls1.HasData() == false);

		WString text = itow(i);
		TEST_ASSERT(tls2.HasData() == false);
		tls2.Set(text.Buffer());
		TEST_ASSERT(tls2.HasData() == true);
		TEST_ASSERT(tls2.Get() == text.Buffer());
		tls2.Clear();
		TEST_ASSERT(tls2.HasData() == false);

		TEST_ASSERT(tls3.HasData() == false);
		tls3.Set(text);
		TEST_ASSERT(tls3.HasData() == true);
		TEST_ASSERT(tls3.Get() == text);
		tls3.Clear();
		TEST_ASSERT(tls3.HasData() == false);

		tls1.Set(0);
		tls2.Set(L"");
		tls3.Set(L"");

		INCRC(&counter);
	}
}
using namespace mynamespace;

TEST_FILE
{
	TEST_CASE(L"Test Thread")
	{
		ThreadData data;
		Thread* thread = Thread::CreateAndStart(SimpleThreadProc, &data, false);
		TEST_ASSERT(thread->GetState() == Thread::Running);
		TEST_ASSERT(thread->Wait() == true);
		TEST_ASSERT(thread->GetState() == Thread::Stopped);
		delete thread;
		TEST_ASSERT(data.modified == true);
	});

	TEST_CASE(L"Test Mutex")
	{
		Mutex_ThreadData data;
		List<Thread*> threads;
		{
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(Mutex_ThreadProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
			TEST_ASSERT(data.mutex.Release());
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.counter == 10);
	});

	TEST_CASE(L"Test Semaphore")
	{
		Semaphore_ThreadData data(10);
		List<Thread*> threads;
		{
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(Semaphore_ThreadProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
		}
		{
			data.semaphore.Release(3);
			Thread::Sleep(100);
			CriticalSection::Scope lock(data.cs);
			TEST_ASSERT(data.counter == 3);
		}
		{
			data.semaphore.Release(4);
			Thread::Sleep(100);
			CriticalSection::Scope lock(data.cs);
			TEST_ASSERT(data.counter == 7);
		}
		{
			data.semaphore.Release(3);
			Thread::Sleep(100);
			CriticalSection::Scope lock(data.cs);
			TEST_ASSERT(data.counter == 10);
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
	});

	TEST_CASE(L"Test ManualEventObject")
	{
		ManualEvent_ThreadData data;
		List<Thread*> threads;
		{
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(ManualEvent_ThreadProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
			TEST_ASSERT(data.eventObject.Signal());
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.counter == 10);
	});

	TEST_CASE(L"Test AutoEventObject")
	{
		AutoEvent_ThreadData data;
		List<Thread*> threads;
		{
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(AutoEvent_ThreadProc, &data, false));
			}
			Thread::Sleep(100);
			TEST_ASSERT(data.counter == 0);
		}
		for (vint i = 0; i < 10; i++)
		{
			TEST_ASSERT(data.counter == i);
			TEST_ASSERT(data.eventObject.Signal());
			Thread::Sleep(100);
			TEST_ASSERT(data.counter == i + 1);
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.counter == 10);
	});

	TEST_CASE(L"Test CriticalSection")
	{
		CS_ThreadData data;
		List<Thread*> threads;
		{
			CriticalSection::Scope lock(data.cs);
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(CS_ThreadProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.cs.TryEnter());
		TEST_ASSERT(data.counter == 10);
	});

	TEST_CASE(L"Test ReaderWriterLock")
	{
		SRW_ThreadData data;
		List<Thread*> threads;
		{
			threads.Add(Thread::CreateAndStart(SRW_WriterProc, &data, false));
			for (vint i = 0; i < 9; i++)
			{
				threads.Add(Thread::CreateAndStart(SRW_ReaderProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
		}
		data.ev.Signal();
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.counter == 100);
	});

	TEST_CASE(L"Test SpinLock 1")
	{
		SL_ThreadData data;
		List<Thread*> threads;
		{
			SpinLock::Scope lock(data.lock);
			for (vint i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart(SL_ThreadProc, &data, false));
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
		}
		for (auto thread : threads)
		{
			thread->Wait();
			TEST_ASSERT(thread->GetState() == Thread::Stopped);
			delete thread;
		}
		TEST_ASSERT(data.lock.TryEnter());
		TEST_ASSERT(data.counter == 10);
	});

	TEST_CASE(L"Test SpinLock 2")
	{
		SL_ThreadData data;
		{
			SpinLock::Scope lock(data.lock);
			for (vint i = 0; i < 10; i++)
			{
				ThreadPoolLite::QueueLambda([&data]() {SL_ThreadProc(nullptr, &data); });
			}
			Thread::Sleep(1000);
			TEST_ASSERT(data.counter == 0);
		}
		while (data.counter != 10);
		Thread::Sleep(1000);
		TEST_ASSERT(data.lock.TryEnter());
	#ifdef VCZH_GCC
		TEST_ASSERT(ThreadPoolLite::Stop(true));
	#endif
	});

	TEST_CASE(L"Test ThreadLocalStorage")
	{
		ThreadLocalStorage::FixStorages();
		atomic_vint counter = 0;
		List<Thread*> threads;
		{
			for (int i = 0; i < 10; i++)
			{
				threads.Add(Thread::CreateAndStart([i, &counter]()
				{
					TlsProc(i, counter);
				}, false));
			}
		}
		TlsProc(-1, counter);
		Thread::Sleep(1000);
		for (auto thread : threads)
		{
			thread->Wait();
			delete thread;
		}
		TEST_ASSERT(counter == 11);
		ThreadLocalStorage::DisposeStorages();
	});
}