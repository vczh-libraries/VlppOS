/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_THREADING
#define VCZH_THREADING

#include <Vlpp.h>

namespace vl
{
	
/***********************************************************************
Kernel Mode Objects
***********************************************************************/

	namespace threading_internal
	{
		struct WaitableData;
		struct ThreadData;
		struct MutexData;
		struct SemaphoreData;
		struct EventData;
		struct CriticalSectionData;
		struct ReaderWriterLockData;
		struct ConditionVariableData;
	}
	
	/// <summary>Base type of all synchronization objects.</summary>
	class WaitableObject : public Object
	{
#if defined VCZH_MSVC
	private:
		threading_internal::WaitableData*			waitableData;
	protected:
		WaitableObject();
		void										SetData(threading_internal::WaitableData* data);
	public:
		NOT_COPYABLE(WaitableObject);

		/// <summary>Test if the object has already been created. Some of the synchronization objects should initialize itself after the constructor.</summary>
		/// <returns>Returns true if the object has already been created.</returns>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										IsCreated();
		/// <summary>Wait for this object to signal.</summary>
		/// <returns>Returns true if the object is signaled. Returns false if this operation failed.</returns>
		bool										Wait();
		/// <summary>Wait for this object to signal for a period of time.</summary>
		/// <returns>Returns true if the object is signaled. Returns false if this operation failed, including time out.</returns>
		/// <param name="ms">Time in milliseconds.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										WaitForTime(vint ms);
		
		/// <summary>Wait for multiple objects.</summary>
		/// <returns>Returns true if all objects are signaled. Returns false if this operation failed.</returns>
		/// <param name="objects">A pointer to an array to <see cref="WaitableObject"/> pointers.</param>
		/// <param name="count">The number of <see cref="WaitableObject"/> objects in the array.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		static bool									WaitAll(WaitableObject** objects, vint count);
		/// <summary>Wait for multiple objects for a period of time.</summary>
		/// <returns>Returns true if all objects are signaled. Returns false if this operation failed, including time out.</returns>
		/// <param name="objects">A pointer to an array to <see cref="WaitableObject"/> pointers.</param>
		/// <param name="count">The number of <see cref="WaitableObject"/> objects in the array.</param>
		/// <param name="ms">Time in milliseconds.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		static bool									WaitAllForTime(WaitableObject** objects, vint count, vint ms);
		/// <summary>Wait for one of the objects.</summary>
		/// <returns>Returns the index of the first signaled or abandoned object, according to the "abandoned" parameter. Returns -1 if this operation failed.</returns>
		/// <param name="objects">A pointer to an array to <see cref="WaitableObject"/> pointers.</param>
		/// <param name="count">The number of <see cref="WaitableObject"/> objects in the array.</param>
		/// <param name="abandoned">Returns true if the waiting is canceled by an abandoned object. An abandoned object is caused by it's owner thread existing without releasing it.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		static vint									WaitAny(WaitableObject** objects, vint count, bool* abandoned);
		/// <summary>Wait for one of the objects for a period of time.</summary>
		/// <returns>Returns the index of the first signaled or abandoned object, according to the "abandoned" parameter. Returns -1 if this operation failed, including time out.</returns>
		/// <param name="objects">A pointer to an array to <see cref="WaitableObject"/> pointers.</param>
		/// <param name="count">The number of <see cref="WaitableObject"/> objects in the array.</param>
		/// <param name="ms">Time in milliseconds.</param>
		/// <param name="abandoned">Returns true if the waiting is canceled by an abandoned object. An abandoned object is caused by it's owner thread existing without releasing it.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		static vint									WaitAnyForTime(WaitableObject** objects, vint count, vint ms, bool* abandoned);
#elif defined VCZH_GCC
		virtual bool								Wait() = 0;
#endif
	};

	/// <summary>Thread. [M:vl.Thread.CreateAndStart] is the suggested way to create threads.</summary>
	class Thread : public WaitableObject
	{
		friend void InternalThreadProc(Thread* thread);
	public:
		/// <summary>Thread state.</summary>
		enum ThreadState
		{
			/// <summary>The thread has not started.</summary>
			NotStarted,
			/// <summary>The thread is running.</summary>
			Running,
			/// <summary>The thread has been stopped.</summary>
			Stopped
		};

		typedef void(*ThreadProcedure)(Thread*, void*);
	protected:
		threading_internal::ThreadData*				internalData;
		volatile ThreadState						threadState;

		virtual void								Run()=0;

		Thread();
	public:
		~Thread();

		/// <summary>Create a thread using a function pointer.</summary>
		/// <returns>Returns the created thread.</returns>
		/// <param name="procedure">The function pointer.</param>
		/// <param name="argument">The argument to call the function pointer.</param>
		/// <param name="deleteAfterStopped">Set to true (by default) to make the thread delete itself after the job is done. If you set this argument to true, you are not recommended to touch the returned thread pointer in any way.</param>
		static Thread*								CreateAndStart(ThreadProcedure procedure, void* argument=0, bool deleteAfterStopped=true);
		/// <summary>Create a thread using a function object or a lambda expression.</summary>
		/// <returns>Returns the created thread.</returns>
		/// <param name="procedure">The function object or the lambda expression.</param>
		/// <param name="deleteAfterStopped">Set to true (by default) to make the thread delete itself after the job is done. If you set this argument to true, you are not recommended to touch the returned thread pointer in any way.</param>
		static Thread*								CreateAndStart(const Func<void()>& procedure, bool deleteAfterStopped=true);
		/// <summary>Pause the caller thread for a period of time.</summary>
		/// <param name="ms">Time in milliseconds.</param>
		static void									Sleep(vint ms);
		/// <summary>Get the number of logical processors.</summary>
		/// <returns>The number of logical processor.</returns>
		static vint									GetCPUCount();
		/// <summary>Get the current thread id.</summary>
		/// <returns>The current thread id.</returns>
		static vint									GetCurrentThreadId();

		/// <summary>Start the thread.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Start();
#if defined VCZH_GCC
		bool										Wait();
#endif
		/// <summary>Stop the thread.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Stop();
		/// <summary>Get the state of the thread.</summary>
		/// <returns>The state of the thread.</returns>
		ThreadState									GetState();
#ifdef VCZH_MSVC
		void										SetCPU(vint index);
#endif
	};

	/// <summary>Mutex. <see cref="Create"/> or <see cref="Open"/> is required to initialize a mutex.</summary>
	class Mutex : public WaitableObject
	{
	private:
		threading_internal::MutexData*				internalData;
	public:
		Mutex();
		~Mutex();

		/// <summary>Create a mutex.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="owned">Set to true to own the created mutex.</param>
		/// <param name="name">Name of the mutex. If it is not empty, than it is a global named mutex. This argument is ignored in Linux.</param>
		bool										Create(bool owned=false, const WString& name=L"");
		/// <summary>Open an existing global named mutex.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="inheritable">Set to true make the mutex visible to all all child processes. This argument is only used in Windows.</param>
		/// <param name="name">Name of the mutex.</param>
		bool										Open(bool inheritable, const WString& name);

		/// <summary>
		/// Release the mutex.
		/// In Linux, calling Release() more than once between two Wait(), or calling Wait() more than once between two Release(), will results in an undefined behavior.
		/// </summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Release();
#ifdef VCZH_GCC
		bool										Wait();
#endif
	};
	
	/// <summary>Semaphore. <see cref="Create"/> or <see cref="Open"/> is required to initialize a semaphore.</summary>
	class Semaphore : public WaitableObject
	{
	private:
		threading_internal::SemaphoreData*			internalData;
	public:
		Semaphore();
		~Semaphore();
		
		/// <summary>Create a semaphore.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="initialCount">Define the counter of the semaphore.</param>
		/// <param name="maxCount">Define the maximum value of the counter of the semaphore. This argument is only used in Windows.</param>
		/// <param name="name">Name of the semaphore. If it is not empty, than it is a global named semaphore. This argument is ignored in Linux.</param>
		bool										Create(vint initialCount, vint maxCount, const WString& name=L"");
		/// <summary>Open an existing global named semaphore.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="inheritable">Set to true make the semaphore visible to all all child processes. This argument is only used in Windows.</param>
		/// <param name="name">Name of the semaphore.</param>
		bool										Open(bool inheritable, const WString& name);
		
		/// <summary> Release the semaphore once. </summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Release();
		/// <summary> Release the semaphore multiple times. </summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="count">The amout to release.</param>
		vint										Release(vint count);
#ifdef VCZH_GCC
		bool										Wait();
#endif
	};

	/// <summary>Event. <see cref="CreateAutoUnsignal"/> or <see cref="CreateManualUnsignal"/> is required to initialize an event.</summary>
	class EventObject : public WaitableObject
	{
	private:
		threading_internal::EventData*				internalData;
	public:
		EventObject();
		~EventObject();
		
		/// <summary>Create an auto unsignal event. Auto unsignal means, when one thread waits for the event and succeeded, the event will become unsignaled immediately.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="signaled">Set to true make the event signaled at the beginning.</param>
		/// <param name="name">Name of the event. If it is not empty, than it is a global named mutex. This argument is only used in Windows.</param>
		bool										CreateAutoUnsignal(bool signaled, const WString& name=L"");
		/// <summary>Create a manual unsignal event.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="signaled">Set to true make the event signaled at the beginning.</param>
		/// <param name="name">Name of the event. If it is not empty, than it is a global named mutex. This argument is only used in Windows.</param>
		bool										CreateManualUnsignal(bool signaled, const WString& name=L"");
		/// <summary>Open an existing global named event.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="inheritable">Set to true make the event visible to all all child processes. This argument is only used in Windows.</param>
		/// <param name="name">Name of the event. This argument is only used in Windows.</param>
		bool										Open(bool inheritable, const WString& name);

		/// <summary>Signal the event.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Signal();
		/// <summary>Unsignal the event.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		bool										Unsignal();
#ifdef VCZH_GCC
		bool										Wait();
#endif
	};

/***********************************************************************
Thread Pool
***********************************************************************/

	/// <summary>A light-weight thread pool.</summary>
	class ThreadPoolLite : public Object
	{
	private:
		ThreadPoolLite();
		~ThreadPoolLite();
	public:
		/// <summary>Queue a function pointer.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="proc">The function pointer.</param>
		/// <param name="argument">The argument to call the function pointer.</param>
		static bool									Queue(void(*proc)(void*), void* argument);
		/// <summary>Queue a function object.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="proc">The function object.</param>
		static bool									Queue(const Func<void()>& proc);
		
		/// <summary>Queue a lambda expression.</summary>
		/// <typeparam name="T">The type of the lambda expression.</typeparam>
		/// <param name="proc">The lambda expression.</param>
		template<typename T>
		static void QueueLambda(const T& proc)
		{
			Queue(Func<void()>(proc));
		}

#ifdef VCZH_GCC
		static bool									Stop(bool discardPendingTasks);
#endif
	};

/***********************************************************************
Kernel Mode Objects in Process
***********************************************************************/

	/// <summary>
	/// Critical section.
	/// The macro "CS_LOCK" is recommended instead of calling [M:vl.CriticalSection.Enter] and [M:vl.CriticalSection.Leave] like this:
	/// <program><code><![CDATA[
	/// CS_LOCK(yourCriticalSection)
	/// {
	///     // do something
	/// }
	/// ]]></code></program>
	/// </summary>
	/// <remarks>
	/// In Windows, enter a owned critical section will not result in dead lock.
	/// In Linux and macOS, it works like a mutex.
	/// </remarks>
	class CriticalSection : public Object
	{
	private:
		friend class ConditionVariable;
		threading_internal::CriticalSectionData*	internalData;
	public:
		NOT_COPYABLE(CriticalSection);
		/// <summary>Create a critical section.</summary>
		CriticalSection();
		~CriticalSection();

		/// <summary>Try enter a critical section. This function will return immediately.</summary>
		/// <returns>Returns true if the current thread owned the critical section.</returns>
		bool										TryEnter();
		/// <summary>Enter a critical section.</summary>
		void										Enter();
		/// <summary>Leave a critical section.</summary>
		void										Leave();

	public:
		class Scope : public Object
		{
		private:
			CriticalSection*						criticalSection;
		public:
			NOT_COPYABLE(Scope);
			Scope(CriticalSection& _criticalSection);
			~Scope();
		};
	};
	
	/// <summary>
	/// Reader writer lock.
	/// The macro "READER_LOCK" and "WRITER_LOCK" are recommended instead of calling [M:vl.ReaderWriterLock.EnterReader], [M:vl.ReaderWriterLock.LeaveReader], [M:vl.ReaderWriterLock.EnterWriter] and [M:vl.ReaderWriterLock.LeaveWriter] like this:
	/// <program><code><![CDATA[
	/// READER_LOCK(yourLock)
	/// {
	///     // do something
	/// }
	/// ]]></code></program>
	/// or
	/// <program><code><![CDATA[
	/// WRITER_LOCK(yourLock)
	/// {
	///     // do something
	/// }
	/// ]]></code></program>
	/// </summary>
	class ReaderWriterLock : public Object
	{
	private:
		friend class ConditionVariable;
		threading_internal::ReaderWriterLockData*	internalData;
	public:
		NOT_COPYABLE(ReaderWriterLock);
		/// <summary>Create a reader writer lock.</summary>
		ReaderWriterLock();
		~ReaderWriterLock();
		
		/// <summary>Try acquire a reader lock. This function will return immediately.</summary>
		/// <returns>Returns true if the current thread acquired the reader lock.</returns>
		bool										TryEnterReader();
		/// <summary>Acquire a reader lock.</summary>
		void										EnterReader();
		/// <summary>Release a reader lock.</summary>
		void										LeaveReader();
		/// <summary>Try acquire a writer lock. This function will return immediately.</summary>
		/// <returns>Returns true if the current thread acquired the writer lock.</returns>
		bool										TryEnterWriter();
		/// <summary>Acquire a writer lock.</summary>
		void										EnterWriter();
		/// <summary>Release a writer lock.</summary>
		void										LeaveWriter();
	public:
		class ReaderScope : public Object
		{
		private:
			ReaderWriterLock*						lock;
		public:
			NOT_COPYABLE(ReaderScope);
			ReaderScope(ReaderWriterLock& _lock);
			~ReaderScope();
		};
		
		class WriterScope : public Object
		{
		private:
			ReaderWriterLock*						lock;
		public:
			NOT_COPYABLE(WriterScope);
			WriterScope(ReaderWriterLock& _lock);
			~WriterScope();
		};
	};

	/// <summary>Conditional variable.</summary>
	class ConditionVariable : public Object
	{
	private:
		threading_internal::ConditionVariableData*	internalData;
	public:
		NOT_COPYABLE(ConditionVariable);
		/// <summary>Create a conditional variable.</summary>
		ConditionVariable();
		~ConditionVariable();

		/// <summary>Bind a conditional variable with a owned critical section and release it. When the function returns, the condition variable is activated, and the current thread owned the critical section again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="cs">The critical section.</param>
		bool										SleepWith(CriticalSection& cs);
#ifdef VCZH_MSVC
		/// <summary>Bind a conditional variable with a owned critical section and release it for a period of time. When the function returns, the condition variable is activated or it is time out, and the current thread owned the critical section again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="cs">The critical section.</param>
		/// <param name="ms">Time in milliseconds.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										SleepWithForTime(CriticalSection& cs, vint ms);
		/// <summary>Bind a conditional variable with a owned reader lock and release it. When the function returns, the condition variable is activated, and the current thread owned the reader lock again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="lock">The reader lock.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										SleepWithReader(ReaderWriterLock& lock);
		/// <summary>Bind a conditional variable with a owned reader lock and release it for a period of time. When the function returns, the condition variable is activated or it is time out, and the current thread owned the reader lock again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="lock">The reader lock.</param>
		/// <param name="ms">Time in milliseconds.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										SleepWithReaderForTime(ReaderWriterLock& lock, vint ms);
		/// <summary>Bind a conditional variable with a owned writer lock and release it. When the function returns, the condition variable is activated, and the current thread owned the writer lock again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="lock">The writer lock.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										SleepWithWriter(ReaderWriterLock& lock);
		/// <summary>Bind a conditional variable with a owned writer lock and release it for a period of time. When the function returns, the condition variable is activated or it is time out, and the current thread owned the writer lock again.</summary>
		/// <returns>Returns true if this operation succeeded.</returns>
		/// <param name="lock">The writer lock.</param>
		/// <param name="ms">Time in milliseconds.</param>
		/// <remarks>This function is only available in Windows.</remarks>
		bool										SleepWithWriterForTime(ReaderWriterLock& lock, vint ms);
#endif
		/// <summary>Wake one thread that pending on this condition variable.</summary>
		void										WakeOnePending();
		/// <summary>Wake all thread that pending on this condition variable.</summary>
		void										WakeAllPendings();
	};

/***********************************************************************
User Mode Objects
***********************************************************************/
	
	/// <summary>
	/// Spin lock. It is similar to mutex, but it does not occupy resource in the system.
	/// The macro "SPIN_LOCK" is recommended instead of calling [M:vl.SpinLock.Enter] and [M:vl.SpinLock.Leave] like this:
	/// <program><code><![CDATA[
	/// SPIN_LOCK(yourLock)
	/// {
	///     // do something
	/// }
	/// ]]></code></program>
	/// </summary>
	class SpinLock : public Object
	{
	protected:
		atomic_vint									token = 0;
	public:
		NOT_COPYABLE(SpinLock);
		/// <summary>Create a spin lock.</summary>
		SpinLock() = default;
		~SpinLock() = default;
		
		/// <summary>Try enter a spin lock. This function will return immediately.</summary>
		/// <returns>Returns true if the current thread owned the spin lock.</returns>
		bool										TryEnter();
		/// <summary>Enter a spin lock.</summary>
		void										Enter();
		/// <summary>Leave a spin lock.</summary>
		void										Leave();

	public:
		class Scope : public Object
		{
		private:
			SpinLock*								spinLock;
		public:
			NOT_COPYABLE(Scope);
			Scope(SpinLock& _spinLock);
			~Scope();
		};
	};

#define SPIN_LOCK(LOCK) SCOPE_VARIABLE(const SpinLock::Scope&, scope, LOCK)
#define CS_LOCK(LOCK) SCOPE_VARIABLE(const CriticalSection::Scope&, scope, LOCK)
#define READER_LOCK(LOCK) SCOPE_VARIABLE(const ReaderWriterLock::ReaderScope&, scope, LOCK)
#define WRITER_LOCK(LOCK) SCOPE_VARIABLE(const ReaderWriterLock::WriterScope&, scope, LOCK)

/***********************************************************************
Thread Local Storage
***********************************************************************/

	/// <summary>Thread local storage operations.</summary>
	/// <remarks>
	/// This class is designed to define global variables.
	/// Dynamically allocation will result in undefined behavior.
	/// </remarks>
	class ThreadLocalStorage : public Object
	{
		typedef void(*Destructor)(void*);
	protected:
		vuint64_t								key;
		Destructor								destructor;
		volatile bool							disposed = false;
		
		static void								PushStorage(ThreadLocalStorage* storage);
	public:
		NOT_COPYABLE(ThreadLocalStorage);
		ThreadLocalStorage(Destructor _destructor);
		~ThreadLocalStorage();

		void*									Get();
		void									Set(void* data);
		void									Clear();
		void									Dispose();

		/// <summary>Fix all storage creation.</summary>
		static void								FixStorages();
		/// <summary>Clear all storages for the current thread. For threads that are created using [T:vl.Thread], this function will be automatically called when before the thread exit.</summary>
		static void								ClearStorages();
		/// <summary>Clear all storages for the current thread (should be the main thread) and clear all records. This function can only be called by the main thread when all other threads are exited. It will reduce noices for detecting memory leaks.</summary>
		static void								DisposeStorages();
	};

	/// <summary>Thread local variable. Different threads can store different values to and obtain differnt values from a thread local variable.</summary>
	/// <typeparam name="T">Type of the storage.</typeparam>
	/// <remarks>
	/// This class is designed to define global variables.
	/// Dynamically allocation will result in undefined behavior.
	/// </remarks>
	template<typename T>
	class ThreadVariable : public Object
	{
	protected:
		ThreadLocalStorage						storage;

		static void Destructor(void* data)
		{
			if (data)
			{
				delete (T*)data;
			}
		}
	public:
		NOT_COPYABLE(ThreadVariable);

		/// <summary>Create a thread local variable.</summary>
		ThreadVariable()
			:storage(&Destructor)
		{
		}

		~ThreadVariable()
		{
		}

		/// <summary>Test if the storage has data.</summary>
		/// <returns>Returns true if the storage has data.</returns>
		bool HasData()
		{
			return storage.Get() != nullptr;
		}

		/// <summary>Remove the data from this storage.</summary>
		void Clear()
		{
			storage.Clear();
		}

		/// <summary>Get the stored data.</summary>
		/// <returns>The stored ata.</returns>
		T& Get()
		{
			return *(T*)storage.Get();
		}

		/// <summary>Set data to this storage.</summary>
		/// <param name="value">The data to set.</param>
		void Set(const T& value)
		{
			storage.Clear();
			storage.Set(new T(value));
		}
	};

	template<typename T>
	class ThreadVariable<T*> : public Object
	{
	protected:
		ThreadLocalStorage						storage;

	public:
		NOT_COPYABLE(ThreadVariable);

		ThreadVariable()
			:storage(nullptr)
		{
		}

		~ThreadVariable()
		{
		}

		bool HasData()
		{
			return storage.Get() != nullptr;
		}

		void Clear()
		{
			storage.Set(nullptr);
		}

		T* Get()
		{
			return (T*)storage.Get();
		}

		void Set(T* value)
		{
			storage.Set((void*)value);
		}
	};

/***********************************************************************
RepeatingTaskExecutor
***********************************************************************/

	/// <summary>
	/// Queued task executor. It is different from a thread because:
	/// <ul>
	///   <li>Task execution is single threaded.</li>
	///   <li>If you queue a task, it will override all unexecuted queued tasks.</li>
	/// </ul>
	/// </summary>
	/// <typeparam name="T">The type of the argument to run a task.</typeparam>
	template<typename T>
	class RepeatingTaskExecutor : public Object
	{
	private:
		SpinLock								inputLock;
		T										inputData;
		volatile bool							inputDataAvailable;
		SpinLock								executingEvent;
		volatile bool							executing;

		void ExecutingProcInternal()
		{
			while(true)
			{
				bool currentInputDataAvailable;
				T currentInputData;
				SPIN_LOCK(inputLock)
				{
					currentInputData=inputData;
					inputData=T();
					currentInputDataAvailable=inputDataAvailable;
					inputDataAvailable=false;
					if(!currentInputDataAvailable)
					{
						executing=false;
						goto FINISH_EXECUTING;
					}
				}
				Execute(currentInputData);
			}
		FINISH_EXECUTING:
			executingEvent.Leave();
		}

		static void ExecutingProc(void* argument)
		{
			((RepeatingTaskExecutor<T>*)argument)->ExecutingProcInternal();
		}
	
	protected:
		/// <summary>This function is called when it is ready to execute a task. Task execution is single threaded. All task code should be put inside the function.</summary>
		/// <param name="input">The argument to run a task.</param>
		virtual void							Execute(const T& input)=0;

	public:
		/// <summary>Create a task executor.</summary>
		RepeatingTaskExecutor()
			:inputDataAvailable(false)
			,executing(false)
		{
		}

		~RepeatingTaskExecutor()
		{
			EnsureTaskFinished();
		}

		/// <summary>Wait for all tasks to finish.</summary>
		void EnsureTaskFinished()
		{
			executingEvent.Enter();
			executingEvent.Leave();
		}

		/// <summary>Queue a task.</summary>
		/// <param name="input">The argument to run a task.</param>
		/// <remarks>
		/// <p>
		/// When there is a running task, queuing a new task will cancel all unexecuted queued tasks.
		/// When there is no running task, queuing a task will execute this task immediately.
		/// </p>
		/// </remarks>
		void SubmitTask(const T& input)
		{
			SPIN_LOCK(inputLock)
			{
				inputData=input;
				inputDataAvailable=true;
			}
			if(!executing)
			{
				executing=true;
				executingEvent.Enter();
				ThreadPoolLite::Queue(&ExecutingProc, this);
			}
		}
	};
}
#endif
