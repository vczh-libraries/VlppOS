﻿/***********************************************************************
THIS FILE IS AUTOMATICALLY GENERATED. DO NOT MODIFY
DEVELOPER: Zihan Chen(vczh)
***********************************************************************/
#include "VlppOS.h"
#include "Vlpp.h"

/***********************************************************************
.\FILESYSTEM.LINUX.CPP
***********************************************************************/
/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	namespace filesystem
	{
		using namespace collections;
		using namespace stream;

/***********************************************************************
FilePath
***********************************************************************/

		const wchar_t FilePath::Delimiter;

		void FilePath::Initialize()
		{
			{
				Array<wchar_t> buffer(fullPath.Length() + 1);
				wcscpy(&buffer[0], fullPath.Buffer());
				NormalizeDelimiters(buffer);
				fullPath = &buffer[0];
			}

			if (fullPath.Length() == 0)
				fullPath = WString::Unmanaged(L"/");

			if (fullPath[0] != Delimiter)
			{
				char buffer[PATH_MAX] = { 0 };
				getcwd(buffer, PATH_MAX);
				fullPath = atow(AString(buffer)) + WString::FromChar(Delimiter) + fullPath;
			}

			{
				collections::List<WString> components;
				GetPathComponents(fullPath, components);
				for (int i = 0; i < components.Count(); i++)
				{
					if (components[i] == L".")
					{
						components.RemoveAt(i);
						i--;
					}
					else if (components[i] == L"..")
					{
						if (i > 0)
						{
							components.RemoveAt(i);
							components.RemoveAt(i - 1);
							i -= 2;
						}
						else
						{
							throw ArgumentException(L"Illegal path.");
						}
					}
				}

				fullPath = ComponentsToPath(components);
			}

			TrimLastDelimiter(fullPath);
		}

		bool FilePath::IsFile()const
		{
			struct stat info;
			AString path = wtoa(fullPath);
			int result = stat(path.Buffer(), &info);
			if(result != 0) return false;
			else return S_ISREG(info.st_mode);
		}

		bool FilePath::IsFolder()const
		{
			struct stat info;
			AString path = wtoa(fullPath);
			int result = stat(path.Buffer(), &info);
			if(result != 0) return false;
			else return S_ISDIR(info.st_mode);
		}

		bool FilePath::IsRoot()const
		{
			return fullPath == L"/";
		}

		WString FilePath::GetRelativePathFor(const FilePath& _filePath) const
		{
			if (fullPath.Length() == 0 || _filePath.fullPath.Length() == 0 || fullPath[0] != _filePath.fullPath[0])
			{
				return _filePath.fullPath;
			}

			collections::List<WString> srcComponents, tgtComponents, resultComponents;
			GetPathComponents(IsFolder() ? fullPath : GetFolder().GetFullPath(), srcComponents);
			GetPathComponents(_filePath.fullPath, tgtComponents);

			int minLength = srcComponents.Count() <= tgtComponents.Count() ? srcComponents.Count() : tgtComponents.Count();
			int lastCommonComponent = 0;
			for (int i = 0; i < minLength; i++)
			{
				if (srcComponents[i] == tgtComponents[i])
				{
					lastCommonComponent = i;
				}
				else
					break;
			}

			for (int i = lastCommonComponent + 1; i < srcComponents.Count(); i++)
			{
				resultComponents.Add(L"..");
			}

			for (int i = lastCommonComponent + 1; i < tgtComponents.Count(); i++)
			{
				resultComponents.Add(tgtComponents[i]);
			}

			return ComponentsToPath(resultComponents);
		}

/***********************************************************************
File
***********************************************************************/

		bool File::Delete()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return unlink(path.Buffer()) == 0;
		}

		bool File::Rename(const WString& newName)const
		{
			AString oldFileName = wtoa(filePath.GetFullPath());
			AString newFileName = wtoa((filePath.GetFolder() / newName).GetFullPath());
			return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
		}

/***********************************************************************
Folder
***********************************************************************/

		bool Folder::GetFolders(collections::List<Folder>& folders)const
		{
			if (!Exists()) return false;

			DIR *dir;
			AString searchPath = wtoa(filePath.GetFullPath());

			if ((dir = opendir(searchPath.Buffer())) == NULL)
			{
				return false;
			}

			struct dirent* entry;
			while ((entry = readdir(dir)) != NULL)
			{
				WString childName = atow(AString(entry->d_name));
				FilePath childFullPath = filePath / childName;
				if (childName != L"." && childName != L".." && childFullPath.IsFolder())
				{
					folders.Add(Folder(childFullPath));
				}
			}

			if (closedir(dir) != 0)
			{
				return false;
			}

			return true;
		}

		bool Folder::GetFiles(collections::List<File>& files)const
		{
			if (!Exists()) return false;

			DIR* dir;
			AString searchPath = wtoa(filePath.GetFullPath());

			if ((dir = opendir(searchPath.Buffer())) == NULL)
			{
				return false;
			}

			struct dirent* entry;
			while ((entry = readdir(dir)) != NULL)
			{
				FilePath childFullPath = filePath / (atow(AString(entry->d_name)));
				if (childFullPath.IsFile())
				{
					files.Add(File(childFullPath));
				}
			}

			if (closedir(dir) != 0)
			{
				return false;
			}

			return true;
		}

		bool Folder::CreateNonRecursively()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return mkdir(path.Buffer(), 0777) == 0;
		}

		bool Folder::DeleteNonRecursively()const
		{
			AString path = wtoa(filePath.GetFullPath());
			return rmdir(path.Buffer()) == 0;
		}

		bool Folder::Rename(const WString& newName)const
		{
			AString oldFileName = wtoa(filePath.GetFullPath());
			AString newFileName = wtoa((filePath.GetFolder() / newName).GetFullPath());
			return rename(oldFileName.Buffer(), newFileName.Buffer()) == 0;
		}
	}
}


/***********************************************************************
.\LOCALE.LINUX.CPP
***********************************************************************/
/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <wctype.h>
#include <wchar.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	using namespace collections;

/***********************************************************************
Locale
***********************************************************************/

	Locale Locale::Invariant()
	{
		return Locale(L"");
	}

	Locale Locale::SystemDefault()
	{
		return Locale(L"en-US");
	}

	Locale Locale::UserDefault()
	{
		return Locale(L"en-US");
	}

	void Locale::Enumerate(collections::List<Locale>& locales)
	{
		locales.Add(Locale(L"en-US"));
	}

	void Locale::GetShortDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"MM/dd/yyyy");
		formats.Add(L"yyyy-MM-dd");
	}

	void Locale::GetLongDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"dddd, dd MMMM yyyy");
	}

	void Locale::GetYearMonthDateFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"yyyy MMMM");
	}

	void Locale::GetLongTimeFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"HH:mm:ss");
	}

	void Locale::GetShortTimeFormats(collections::List<WString>& formats)const
	{
		formats.Add(L"HH:mm");
		formats.Add(L"hh:mm tt");
	}

	WString Locale::FormatDate(const WString& format, DateTime date)const
	{
		/*
		auto df = L"yyyy,MM,MMM,MMMM,dd,ddd,dddd";
		auto ds = L"2000,01,Jan,January,02,Sun,Sunday";
		auto tf = L"hh,HH,mm,ss,tt";
		auto ts = L"01,13,02,03,PM";
		*/
		WString result;
		const wchar_t* reading = format.Buffer();

		while (*reading)
		{
			if (wcsncmp(reading, L"yyyy", 4) == 0)
			{
				WString fragment = itow(date.year);
				while (fragment.Length() < 4) fragment = L"0" + fragment;
				result += fragment;
				reading += 4;
			}
			else if (wcsncmp(reading, L"MMMM", 4) == 0)
			{
				result += GetLongMonthName(date.month);
				reading += 4;
			}
			else if (wcsncmp(reading, L"MMM", 3) == 0)
			{
				result += GetShortMonthName(date.month);
				reading += 3;
			}
			else if (wcsncmp(reading, L"MM", 2) == 0)
			{
				WString fragment = itow(date.month);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"dddd", 4) == 0)
			{
				result += GetLongDayOfWeekName(date.dayOfWeek);
				reading += 4;
			}
			else if (wcsncmp(reading, L"ddd", 3) == 0)
			{
				result += GetShortDayOfWeekName(date.dayOfWeek);
				reading += 3;
			}
			else if (wcsncmp(reading, L"dd", 2) == 0)
			{
				WString fragment = itow(date.day);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"hh", 2) == 0)
			{
				WString fragment = itow(date.hour > 12 ? date.hour - 12 : date.hour);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"HH", 2) == 0)
			{
				WString fragment = itow(date.hour);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"mm", 2) == 0)
			{
				WString fragment = itow(date.minute);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"ss", 2) == 0)
			{
				WString fragment = itow(date.second);
				while (fragment.Length() < 2) fragment = L"0" + fragment;
				result += fragment;
				reading += 2;
			}
			else if (wcsncmp(reading, L"tt", 2) == 0)
			{
				result += date.hour > 12 ? L"PM" : L"AM";
				reading += 2;
			}
			else
			{
				result += WString::FromChar(*reading);
				reading++;
			}
		}
		return result;
	}

	WString Locale::FormatTime(const WString& format, DateTime time)const
	{
		return FormatDate(format, time);
	}

	WString Locale::FormatNumber(const WString& number)const
	{
		return number;
	}

	WString Locale::FormatCurrency(const WString& currency)const
	{
		return currency;
	}

	WString Locale::GetShortDayOfWeekName(vint dayOfWeek)const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sun";
		case 1: return L"Mon";
		case 2:	return L"Tue";
		case 3:	return L"Wed";
		case 4:	return L"Thu";
		case 5:	return L"Fri";
		case 6:	return L"Sat";
		}
		return L"";
	}

	WString Locale::GetLongDayOfWeekName(vint dayOfWeek)const
	{
		switch (dayOfWeek)
		{
		case 0: return L"Sunday";
		case 1: return L"Monday";
		case 2:	return L"Tuesday";
		case 3:	return L"Wednesday";
		case 4:	return L"Thursday";
		case 5:	return L"Friday";
		case 6:	return L"Saturday";
		}
		return L"";
	}

	WString Locale::GetShortMonthName(vint month)const
	{
		switch (month)
		{
		case 1: return L"Jan";
		case 2: return L"Feb";
		case 3: return L"Mar";
		case 4: return L"Apr";
		case 5: return L"May";
		case 6: return L"Jun";
		case 7: return L"Jul";
		case 8: return L"Aug";
		case 9: return L"Sep";
		case 10: return L"Oct";
		case 11: return L"Nov";
		case 12: return L"Dec";
		}
		return L"";
	}

	WString Locale::GetLongMonthName(vint month)const
	{
		switch (month)
		{
		case 1: return L"January";
		case 2: return L"February";
		case 3: return L"March";
		case 4: return L"April";
		case 5: return L"May";
		case 6: return L"June";
		case 7: return L"July";
		case 8: return L"August";
		case 9: return L"September";
		case 10: return L"October";
		case 11: return L"November";
		case 12: return L"December";
		}
		return L"";
	}

	WString Locale::ToLower(const WString& str)const
	{
		return wlower(str);
	}

	WString Locale::ToUpper(const WString& str)const
	{
		return wupper(str);
	}

	WString Locale::ToLinguisticLower(const WString& str)const
	{
		return wlower(str);
	}

	WString Locale::ToLinguisticUpper(const WString& str)const
	{
		return wupper(str);
	}

	vint Locale::Compare(const WString& s1, const WString& s2, Normalization normalization)const
	{
		switch (normalization)
		{
		case Normalization::None:
			return wcscmp(s1.Buffer(), s2.Buffer());
		case Normalization::IgnoreCase:
			return wcscasecmp(s1.Buffer(), s2.Buffer());
		default:
			return 0;
		}
	}

	vint Locale::CompareOrdinal(const WString& s1, const WString& s2)const
	{
		return wcscmp(s1.Buffer(), s2.Buffer());
	}

	vint Locale::CompareOrdinalIgnoreCase(const WString& s1, const WString& s2)const
	{
		return wcscasecmp(s1.Buffer(), s2.Buffer());
	}

	collections::Pair<vint, vint> Locale::FindFirst(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = 0;
		switch (normalization)
		{
		case Normalization::None:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
						break;
					}
					reading++;
				}
			}
			break;
		case Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncasecmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
						break;
					}
					reading++;
				}
			}
			break;
		}
		return result == nullptr ? Pair<vint, vint>(-1, 0) : Pair<vint, vint>(result - text.Buffer(), find.Length());
	}

	collections::Pair<vint, vint> Locale::FindLast(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return Pair<vint, vint>(-1, 0);
		}
		const wchar_t* result = 0;
		switch (normalization)
		{
		case Normalization::None:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
					}
					reading++;
				}
			}
			break;
		case Normalization::IgnoreCase:
			{
				const wchar_t* reading = text.Buffer();
				while (*reading)
				{
					if (wcsncasecmp(reading, find.Buffer(), find.Length()) == 0)
					{
						result = reading;
					}
					reading++;
				}
			}
			break;
		}
		return result == nullptr ? Pair<vint, vint>(-1, 0) : Pair<vint, vint>(result - text.Buffer(), find.Length());
	}

	bool Locale::StartsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Normalization::None:
			return wcsncmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		case Normalization::IgnoreCase:
			return wcsncasecmp(text.Buffer(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}

	bool Locale::EndsWith(const WString& text, const WString& find, Normalization normalization)const
	{
		if (text.Length() < find.Length() || find.Length() == 0)
		{
			return false;
		}
		switch (normalization)
		{
		case Normalization::None:
			return wcsncmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		case Normalization::IgnoreCase:
			return wcsncasecmp(text.Buffer() + text.Length() - find.Length(), find.Buffer(), find.Length()) == 0;
		}
		return false;
	}
}


/***********************************************************************
.\THREADING.LINUX.CPP
***********************************************************************/
/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include <pthread.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>
#if defined(__APPLE__) || defined(__APPLE_CC__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	using namespace threading_internal;
	using namespace collections;


/***********************************************************************
Thread
***********************************************************************/

	namespace threading_internal
	{
		struct ThreadData
		{
			pthread_t					id;
			EventObject					ev;
		};

		class ProceduredThread : public Thread
		{
		private:
			Thread::ThreadProcedure		procedure;
			void*						argument;
			bool						deleteAfterStopped;

		protected:
			void Run()
			{
				bool deleteAfterStopped = this->deleteAfterStopped;
				ThreadLocalStorage::FixStorages();
				try
				{
					procedure(this, argument);
					threadState=Thread::Stopped;
					internalData->ev.Signal();
					ThreadLocalStorage::ClearStorages();
				}
				catch (...)
				{
					ThreadLocalStorage::ClearStorages();
					throw;
				}
				if(deleteAfterStopped)
				{
					delete this;
				}
			}
		public:
			ProceduredThread(Thread::ThreadProcedure _procedure, void* _argument, bool _deleteAfterStopped)
				:procedure(_procedure)
				,argument(_argument)
				,deleteAfterStopped(_deleteAfterStopped)
			{
			}
		};

		class LambdaThread : public Thread
		{
		private:
			Func<void()>				procedure;
			bool						deleteAfterStopped;

		protected:
			void Run()
			{
				bool deleteAfterStopped = this->deleteAfterStopped;
				ThreadLocalStorage::FixStorages();
				try
				{
					procedure();
					threadState=Thread::Stopped;
					internalData->ev.Signal();
					ThreadLocalStorage::ClearStorages();
				}
				catch (...)
				{
					ThreadLocalStorage::ClearStorages();
					throw;
				}
				if(deleteAfterStopped)
				{
					delete this;
				}
			}
		public:
			LambdaThread(const Func<void()>& _procedure, bool _deleteAfterStopped)
				:procedure(_procedure)
				,deleteAfterStopped(_deleteAfterStopped)
			{
			}
		};
	}

	void InternalThreadProc(Thread* thread)
	{
		thread->Run();
	}

	void* InternalThreadProcWrapper(void* lpParameter)
	{
		InternalThreadProc((Thread*)lpParameter);
		return 0;
	}

	Thread::Thread()
	{
		internalData=new ThreadData;
		internalData->ev.CreateManualUnsignal(false);
		threadState=Thread::NotStarted;
	}

	Thread::~Thread()
	{
		if (internalData)
		{
			Stop();
			if (threadState!=Thread::NotStarted)
			{
				pthread_detach(internalData->id);
			}
			delete internalData;
		}
	}

	Thread* Thread::CreateAndStart(ThreadProcedure procedure, void* argument, bool deleteAfterStopped)
	{
		if(procedure)
		{
			Thread* thread=new ProceduredThread(procedure, argument, deleteAfterStopped);
			if(thread->Start())
			{
				return thread;
			}
			else
			{
				delete thread;
			}
		}
		return 0;
	}

	Thread* Thread::CreateAndStart(const Func<void()>& procedure, bool deleteAfterStopped)
	{
		Thread* thread=new LambdaThread(procedure, deleteAfterStopped);
		if(thread->Start())
		{
			return thread;
		}
		else
		{
			delete thread;
		}
		return 0;
	}

	void Thread::Sleep(vint ms)
	{
		if (ms >= 1000)
		{
			sleep(ms / 1000);
		}
		if (ms % 1000)
		{
			usleep((ms % 1000) * 1000);
		}
	}
	
	vint Thread::GetCPUCount()
	{
		return (vint)sysconf(_SC_NPROCESSORS_ONLN);
	}

	vint Thread::GetCurrentThreadId()
	{
		return (vint)::pthread_self();
	}

	bool Thread::Start()
	{
		if(threadState==Thread::NotStarted)
		{
			if(pthread_create(&internalData->id, nullptr, &InternalThreadProcWrapper, this)==0)
			{
				threadState=Thread::Running;
				return true;
			}
		}
		return false;
	}

	bool Thread::Wait()
	{
		return internalData->ev.Wait();
	}

	bool Thread::Stop()
	{
		if (threadState==Thread::Running)
		{
			if(pthread_cancel(internalData->id)==0)
			{
				threadState=Thread::Stopped;
				internalData->ev.Signal();
				return true;
			}
		}
		return false;
	}

	Thread::ThreadState Thread::GetState()
	{
		return threadState;
	}

/***********************************************************************
Mutex
***********************************************************************/

	namespace threading_internal
	{
		struct MutexData
		{
			Semaphore			sem;
		};
	};

	Mutex::Mutex()
	{
		internalData = new MutexData;
	}

	Mutex::~Mutex()
	{
		delete internalData;
	}

	bool Mutex::Create(bool owned, const WString& name)
	{
		return internalData->sem.Create(owned ? 0 : 1, 1, name);
	}

	bool Mutex::Open(bool inheritable, const WString& name)
	{
		return internalData->sem.Open(inheritable, name);
	}

	bool Mutex::Release()
	{
		return internalData->sem.Release();
	}

	bool Mutex::Wait()
	{
		return internalData->sem.Wait();
	}

/***********************************************************************
Semaphore
***********************************************************************/

	namespace threading_internal
	{
		struct SemaphoreData
		{
			sem_t			semUnnamed;
			sem_t*			semNamed = nullptr;
		};
	}

	Semaphore::Semaphore()
		:internalData(0)
	{
	}

	Semaphore::~Semaphore()
	{
		if (internalData)
		{
			if (internalData->semNamed)
			{
				sem_close(internalData->semNamed);
			}
			else
			{
				sem_destroy(&internalData->semUnnamed);
			}
			delete internalData;
		}
	}

	bool Semaphore::Create(vint initialCount, vint maxCount, const WString& name)
	{
		if (internalData) return false;
		if (initialCount > maxCount) return false;

		internalData = new SemaphoreData;
#if defined(__APPLE__)
        
		AString auuid;
		if(name.Length() == 0)
		{
			CFUUIDRef cfuuid = CFUUIDCreate(kCFAllocatorDefault);
			CFStringRef cfstr = CFUUIDCreateString(kCFAllocatorDefault, cfuuid);
			auuid = CFStringGetCStringPtr(cfstr, kCFStringEncodingASCII);

			CFRelease(cfstr);
			CFRelease(cfuuid);
		}
		auuid = auuid.Insert(0, "/");
		// OSX SEM_NAME_LENGTH = 31
		if(auuid.Length() >= 30)
			auuid = auuid.Sub(0, 30);
        
		if ((internalData->semNamed = sem_open(auuid.Buffer(), O_CREAT, O_RDWR, initialCount)) == SEM_FAILED)
		{
			delete internalData;
			internalData = 0;
			return false;
		}
        
#else
		if (name == L"")
		{
			if(sem_init(&internalData->semUnnamed, 0, (int)initialCount) == -1)
			{
				delete internalData;
				internalData = 0;
				return false;
			}
		}
        	else
        	{
			AString astr = wtoa(name);
            
			if ((internalData->semNamed = sem_open(astr.Buffer(), O_CREAT, 0777, initialCount)) == SEM_FAILED)
			{
				delete internalData;
				internalData = 0;
				return false;
			}
		}
#endif

		Release(initialCount);
		return true;
	}

	bool Semaphore::Open(bool inheritable, const WString& name)
	{
		if (internalData) return false;
		if (inheritable) return false;

		internalData = new SemaphoreData;
		if (!(internalData->semNamed = sem_open(wtoa(name).Buffer(), 0)))
		{
            delete internalData;
            internalData = 0;
			return false;
		}

		return true;
	}

	bool Semaphore::Release()
	{
		return Release(1);
	}

	vint Semaphore::Release(vint count)
	{
		for (vint i = 0; i < count; i++)
		{
			if (internalData->semNamed)
			{
				sem_post(internalData->semNamed);
			}
			else
			{
				sem_post(&internalData->semUnnamed);
			}
		}
		return true;
	}

	bool Semaphore::Wait()
	{
		if (internalData->semNamed)
		{
			return sem_wait(internalData->semNamed) == 0;
		}
		else
		{
			return sem_wait(&internalData->semUnnamed) == 0;
		}
	}

/***********************************************************************
EventObject
***********************************************************************/

	namespace threading_internal
	{
		struct EventData
		{
			bool				autoReset;
			volatile bool		signaled;
			CriticalSection		mutex;
			ConditionVariable	cond;
			atomic_vint			counter = 0;
		};
	}

	EventObject::EventObject()
	{
		internalData = nullptr;
	}

	EventObject::~EventObject()
	{
		if (internalData)
		{
			delete internalData;
		}
	}

	bool EventObject::CreateAutoUnsignal(bool signaled, const WString& name)
	{
		if (name!=L"") return false;
		if (internalData) return false;

		internalData = new EventData;
		internalData->autoReset = true;
		internalData->signaled = signaled;
		return true;
	}

	bool EventObject::CreateManualUnsignal(bool signaled, const WString& name)
	{
		if (name!=L"") return false;
		if (internalData) return false;

		internalData = new EventData;
		internalData->autoReset = false;
		internalData->signaled = signaled;
		return true;
	}

	bool EventObject::Signal()
	{
		if (!internalData) return false;

		internalData->mutex.Enter();
		internalData->signaled = true;
		if (internalData->counter)
		{
			if (internalData->autoReset)
			{
				internalData->cond.WakeOnePending();
				internalData->signaled = false;
			}
			else
			{
				internalData->cond.WakeAllPendings();
			}
		}
		internalData->mutex.Leave();
		return true;
	}

	bool EventObject::Unsignal()
	{
		if (!internalData) return false;

		internalData->mutex.Enter();
		internalData->signaled = false;
		internalData->mutex.Leave();
		return true;
	}

	bool EventObject::Wait()
	{
		if (!internalData) return false;

		internalData->mutex.Enter();
		if (internalData->signaled)
		{
			if (internalData->autoReset)
			{
				internalData->signaled = false;
			}
		}
		else
		{
			INCRC(&internalData->counter);
			internalData->cond.SleepWith(internalData->mutex);
			DECRC(&internalData->counter);
		}
		internalData->mutex.Leave();
		return true;
	}

/***********************************************************************
ThreadPoolLite
***********************************************************************/

	namespace threading_internal
	{
		struct ThreadPoolTask
		{
			Func<void()>			task;
			Ptr<ThreadPoolTask>		next;
		};

		struct ThreadPoolData
		{
			Semaphore				semaphore;
			EventObject				taskFinishEvent;
			Ptr<ThreadPoolTask>		taskBegin;
			Ptr<ThreadPoolTask>*	taskEnd = nullptr;
			volatile bool			stopping = false;
			List<Thread*>			taskThreads;
		};

		SpinLock					threadPoolLock;
		ThreadPoolData*				threadPoolData = nullptr;

		void ThreadPoolProc(Thread* thread, void* argument)
		{
			while (true)
			{
				Ptr<ThreadPoolTask> task;

				threadPoolData->semaphore.Wait();
				SPIN_LOCK(threadPoolLock)
				{
					if (threadPoolData->taskBegin)
					{
						task = threadPoolData->taskBegin;
						threadPoolData->taskBegin = task->next;
					}

					if (!threadPoolData->taskBegin)
					{
						threadPoolData->taskEnd = &threadPoolData->taskBegin;
						threadPoolData->taskFinishEvent.Signal();
					}
				}

				if (task)
				{
					ThreadLocalStorage::FixStorages();
					try
					{
						task->task();
						ThreadLocalStorage::ClearStorages();
					}
					catch (...)
					{
						ThreadLocalStorage::ClearStorages();
					}
				}
				else if (threadPoolData->stopping)
				{
					return;
				}
			}
		}

		bool ThreadPoolQueue(const Func<void()>& proc)
		{
			SPIN_LOCK(threadPoolLock)
			{
				if (!threadPoolData)
				{
					threadPoolData = new ThreadPoolData;
					threadPoolData->semaphore.Create(0, 65536);
					threadPoolData->taskFinishEvent.CreateManualUnsignal(false);
					threadPoolData->taskEnd = &threadPoolData->taskBegin;

					for (vint i = 0; i < Thread::GetCPUCount() * 4; i++)
					{
						threadPoolData->taskThreads.Add(Thread::CreateAndStart(&ThreadPoolProc, nullptr, false));
					}
				}

				if (threadPoolData)
				{
					if (threadPoolData->stopping)
					{
						return false;
					}

					auto task = Ptr(new ThreadPoolTask);
					task->task = proc;
					*threadPoolData->taskEnd = task;
					threadPoolData->taskEnd = &task->next;
					threadPoolData->semaphore.Release();
					threadPoolData->taskFinishEvent.Unsignal();
				}
			}
			return true;
		}

		bool ThreadPoolStop(bool discardPendingTasks)
		{
			SPIN_LOCK(threadPoolLock)
			{
				if (!threadPoolData) return false;
				if (threadPoolData->stopping) return false;

				threadPoolData->stopping = true;
				if (discardPendingTasks)
				{
					threadPoolData->taskEnd = &threadPoolData->taskBegin;
					threadPoolData->taskBegin = nullptr;
				}

				threadPoolData->semaphore.Release(threadPoolData->taskThreads.Count());
			}

			threadPoolData->taskFinishEvent.Wait();
			// TODO: (enumerable) foreach
			for (vint i = 0; i < threadPoolData->taskThreads.Count(); i++)
			{
				auto thread = threadPoolData->taskThreads[i];
				thread->Wait();
				delete thread;
			}
			threadPoolData->taskThreads.Clear();

			SPIN_LOCK(threadPoolLock)
			{
				delete threadPoolData;
				threadPoolData = nullptr;
			}
			return true;
		}
	}

	ThreadPoolLite::ThreadPoolLite()
	{
	}

	ThreadPoolLite::~ThreadPoolLite()
	{
	}

	bool ThreadPoolLite::Queue(void(*proc)(void*), void* argument)
	{
		return ThreadPoolQueue([proc, argument](){proc(argument);});
	}

	bool ThreadPoolLite::Queue(const Func<void()>& proc)
	{
		return ThreadPoolQueue(proc);
	}

	bool ThreadPoolLite::Stop(bool discardPendingTasks)
	{
		return ThreadPoolStop(discardPendingTasks);
	}

/***********************************************************************
CriticalSection
***********************************************************************/

	namespace threading_internal
	{
		struct CriticalSectionData
		{
			pthread_mutex_t		mutex;
		};
	}

	CriticalSection::CriticalSection()
	{
		internalData = new CriticalSectionData;
		pthread_mutex_init(&internalData->mutex, nullptr);
	}

	CriticalSection::~CriticalSection()
	{
		pthread_mutex_destroy(&internalData->mutex);
		delete internalData;
	}

	bool CriticalSection::TryEnter()
	{
		return pthread_mutex_trylock(&internalData->mutex) == 0;
	}

	void CriticalSection::Enter()
	{
		pthread_mutex_lock(&internalData->mutex);
	}

	void CriticalSection::Leave()
	{
		pthread_mutex_unlock(&internalData->mutex);
	}

	CriticalSection::Scope::Scope(CriticalSection& _criticalSection)
		:criticalSection(&_criticalSection)
	{
		criticalSection->Enter();
	}

	CriticalSection::Scope::~Scope()
	{
		criticalSection->Leave();
	}

/***********************************************************************
ReaderWriterLock
***********************************************************************/

	namespace threading_internal
	{
		struct ReaderWriterLockData
		{
			pthread_rwlock_t			rwlock;
		};
	}

	ReaderWriterLock::ReaderWriterLock()
	{
		internalData = new ReaderWriterLockData;
		pthread_rwlock_init(&internalData->rwlock, nullptr);
	}

	ReaderWriterLock::~ReaderWriterLock()
	{
		pthread_rwlock_destroy(&internalData->rwlock);
		delete internalData;
	}

	bool ReaderWriterLock::TryEnterReader()
	{
		return pthread_rwlock_tryrdlock(&internalData->rwlock) == 0;
	}

	void ReaderWriterLock::EnterReader()
	{
		pthread_rwlock_rdlock(&internalData->rwlock);
	}

	void ReaderWriterLock::LeaveReader()
	{
		pthread_rwlock_unlock(&internalData->rwlock);
	}

	bool ReaderWriterLock::TryEnterWriter()
	{
		return pthread_rwlock_trywrlock(&internalData->rwlock) == 0;
	}

	void ReaderWriterLock::EnterWriter()
	{
		pthread_rwlock_wrlock(&internalData->rwlock);
	}

	void ReaderWriterLock::LeaveWriter()
	{
		pthread_rwlock_unlock(&internalData->rwlock);
	}

	ReaderWriterLock::ReaderScope::ReaderScope(ReaderWriterLock& _lock)
		:lock(&_lock)
	{
		lock->EnterReader();
	}

	ReaderWriterLock::ReaderScope::~ReaderScope()
	{
		lock->LeaveReader();
	}

	ReaderWriterLock::WriterScope::WriterScope(ReaderWriterLock& _lock)
		:lock(&_lock)
	{
		lock->EnterWriter();
	}

	ReaderWriterLock::WriterScope::~WriterScope()
	{
		lock->LeaveReader();
	}

/***********************************************************************
ConditionVariable
***********************************************************************/

	namespace threading_internal
	{
		struct ConditionVariableData
		{
			pthread_cond_t			cond;
		};
	}

	ConditionVariable::ConditionVariable()
	{
		internalData = new ConditionVariableData;
		pthread_cond_init(&internalData->cond, nullptr);
	}

	ConditionVariable::~ConditionVariable()
	{
		pthread_cond_destroy(&internalData->cond);
		delete internalData;
	}

	bool ConditionVariable::SleepWith(CriticalSection& cs)
	{
		return pthread_cond_wait(&internalData->cond, &cs.internalData->mutex) == 0;
	}

	void ConditionVariable::WakeOnePending()
	{
		pthread_cond_signal(&internalData->cond);
	}

	void ConditionVariable::WakeAllPendings()
	{
		pthread_cond_broadcast(&internalData->cond);
	}

/***********************************************************************
ThreadLocalStorage
***********************************************************************/

#define KEY ((pthread_key_t&)key)

	ThreadLocalStorage::ThreadLocalStorage(Destructor _destructor)
		:destructor(_destructor)
	{
		static_assert(sizeof(key) >= sizeof(pthread_key_t), "ThreadLocalStorage's key storage is not large enouth.");
		PushStorage(this);
		auto error = pthread_key_create(&KEY, destructor);
		CHECK_ERROR(error != EAGAIN && error != ENOMEM, L"vl::ThreadLocalStorage::ThreadLocalStorage()#Failed to create a thread local storage index.");
	}

	ThreadLocalStorage::~ThreadLocalStorage()
	{
		pthread_key_delete(KEY);
	}

	void* ThreadLocalStorage::Get()
	{
		CHECK_ERROR(!disposed, L"vl::ThreadLocalStorage::Get()#Cannot access a disposed ThreadLocalStorage.");
		return pthread_getspecific(KEY);
	}

	void ThreadLocalStorage::Set(void* data)
	{
		CHECK_ERROR(!disposed, L"vl::ThreadLocalStorage::Set()#Cannot access a disposed ThreadLocalStorage.");
		pthread_setspecific(KEY, data);
	}

#undef KEY
}


/***********************************************************************
.\ENCODING\CHARFORMAT\CHARFORMAT.LINUX.CPP
***********************************************************************/
/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include <string.h>

#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	namespace stream
	{
		using namespace vl::encoding;

		bool IsMbcsLeadByte(char c)
		{
			return (vint8_t)c < 0;
		}

		void MbcsToWChar(wchar_t* wideBuffer, vint wideChars, vint wideReaded, char* mbcsBuffer, vint mbcsChars)
		{
			AString a = AString::CopyFrom(mbcsBuffer, mbcsChars);
			WString w = atow(a);
			memcpy(wideBuffer, w.Buffer(), wideReaded * sizeof(wchar_t));
		}

/***********************************************************************
Mbcs
***********************************************************************/

		vint MbcsEncoder::WriteString(wchar_t* _buffer, vint chars)
		{
			WString w = WString::CopyFrom(_buffer, chars);
			AString a = wtoa(w);
			vint length = a.Length();
			vint result = stream->Write((void*)a.Buffer(), length);

			if (result != length)
			{
				Close();
				return 0;
			}
			return chars;
		}
	}
}


/***********************************************************************
.\ENCODING\CHARFORMAT\CHARFORMAT_TESTENCODING.LINUX.CPP
***********************************************************************/
/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/


#ifndef VCZH_GCC
static_assert(false, "Do not build this file for Windows applications.");
#endif

namespace vl
{
	namespace stream
	{
/***********************************************************************
Helper Functions
***********************************************************************/

		extern bool CanBeMbcs(unsigned char* buffer, vint size);
		extern bool CanBeUtf8(unsigned char* buffer, vint size);
		extern bool CanBeUtf16(unsigned char* buffer, vint size, bool& hitSurrogatePairs);
		extern bool CanBeUtf16BE(unsigned char* buffer, vint size, bool& hitSurrogatePairs);
		
/***********************************************************************
TestEncoding
***********************************************************************/

		extern void TestEncodingInternal(
			unsigned char* buffer,
			vint size,
			BomEncoder::Encoding& encoding,
			bool containsBom,
			bool utf16HitSurrogatePairs,
			bool utf16BEHitSurrogatePairs,
			bool roughMbcs,
			bool roughUtf8,
			bool roughUtf16,
			bool roughUtf16BE
			)
		{
			if (roughUtf16 && roughUtf16BE && !roughUtf8)
			{
				if (utf16BEHitSurrogatePairs && !utf16HitSurrogatePairs)
				{
					encoding = BomEncoder::Utf16BE;
				}
				else
				{
					encoding = BomEncoder::Utf16;
				}
			}
			else
			{
				encoding = BomEncoder::Utf8;
			}
		}
	}
}

