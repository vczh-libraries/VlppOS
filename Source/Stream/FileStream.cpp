/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "FileStream.h"
#include "../FileSystem.h"
#if defined VCZH_GCC
#include <stdio.h>
#endif

namespace vl
{
	namespace filesystem
	{
		extern IFileSystemImpl*			GetFileSystemImpl();
	}

	namespace stream
	{
#if defined VCZH_GCC
		void _fseeki64(FILE* file, pos_t offset, int origin)
		{
			fseek(file, (long)offset, origin);
		}
#endif

/***********************************************************************
OSFileStreamImpl
***********************************************************************/

		class OSFileStreamImpl : public Object, public virtual IFileStreamImpl
		{
		private:
			WString					fileName;
			FileStream::AccessRight	accessRight;
			FILE*					file;

		public:
			OSFileStreamImpl(const WString& _fileName, FileStream::AccessRight _accessRight)
				: fileName(_fileName), accessRight(_accessRight), file(nullptr)
			{
			}

			~OSFileStreamImpl()
			{
				Close();
			}

			bool Open() override
			{
				const wchar_t* mode = L"rb";
				switch(accessRight)
				{
				case FileStream::ReadOnly:
					mode = L"rb";
					break;
				case FileStream::WriteOnly:
					mode = L"wb";
					break;
				case FileStream::ReadWrite:
					mode = L"w+b";
					break;
				}

#if defined VCZH_MSVC
				if(_wfopen_s(&file, fileName.Buffer(), mode) != 0)
				{
					file = nullptr;
					return false;
				}
#elif defined VCZH_GCC
				AString fileNameA = wtoa(fileName);
				AString modeA = wtoa(mode);
				file = fopen(fileNameA.Buffer(), modeA.Buffer());
				if(file == nullptr)
				{
					return false;
				}
#endif
				return true;
			}

			void Close() override
			{
				if(file != nullptr)
				{
					fclose(file);
					file = nullptr;
				}
			}

			pos_t Position() const override
			{
				if(file != nullptr)
				{
#if defined VCZH_MSVC
					fpos_t position = 0;
					if(fgetpos(file, &position) == 0)
					{
						return position;
					}
#elif defined VCZH_GCC
					return (pos_t)ftell(file);
#endif
				}
				return -1;
			}

			pos_t Size() const override
			{
				if(file != nullptr)
				{
#if defined VCZH_MSVC
					fpos_t position = 0;
					if(fgetpos(file, &position) == 0)
					{
						if(fseek(file, 0, SEEK_END) == 0)
						{
							pos_t size = Position();
							if(fsetpos(file, &position) == 0)
							{
								return size;
							}
						}
					}
#elif defined VCZH_GCC
					long position = ftell(file);
					fseek(file, 0, SEEK_END);
					long size = ftell(file);
					fseek(file, position, SEEK_SET);
					return (pos_t)size;
#endif
				}
				return -1;
			}

			void Seek(pos_t _size) override
			{
				if(Position() + _size > Size())
				{
					_fseeki64(file, 0, SEEK_END);
				}
				else if(Position() + _size < 0)
				{
					_fseeki64(file, 0, SEEK_SET);
				}
				else
				{
					_fseeki64(file, _size, SEEK_CUR);
				}
			}

			void SeekFromBegin(pos_t _size) override
			{
				if(_size > Size())
				{
					_fseeki64(file, 0, SEEK_END);
				}
				else if(_size < 0)
				{
					_fseeki64(file, 0, SEEK_SET);
				}
				else
				{
					_fseeki64(file, _size, SEEK_SET);
				}
			}

			void SeekFromEnd(pos_t _size) override
			{
				if(_size < 0)
				{
					_fseeki64(file, 0, SEEK_END);
				}
				else if(_size > Size())
				{
					_fseeki64(file, 0, SEEK_SET);
				}
				else
				{
					_fseeki64(file, -_size, SEEK_END);
				}
			}

			vint Read(void* _buffer, vint _size) override
			{
				CHECK_ERROR(file != nullptr, L"FileStream::Read(pos_t)#Stream is closed, cannot perform this operation.");
				CHECK_ERROR(_size >= 0, L"FileStream::Read(void*, vint)#Argument size cannot be negative.");
				return fread(_buffer, 1, _size, file);
			}

			vint Write(void* _buffer, vint _size) override
			{
				CHECK_ERROR(file != nullptr, L"FileStream::Write(pos_t)#Stream is closed, cannot perform this operation.");
				CHECK_ERROR(_size >= 0, L"FileStream::Write(void*, vint)#Argument size cannot be negative.");
				return fwrite(_buffer, 1, _size, file);
			}

			vint Peek(void* _buffer, vint _size) override
			{
				CHECK_ERROR(file != nullptr, L"FileStream::Peek(pos_t)#Stream is closed, cannot perform this operation.");
				CHECK_ERROR(_size >= 0, L"FileStream::Peek(void*, vint)#Argument size cannot be negative.");
#if defined VCZH_MSVC
				fpos_t position = 0;
				if(fgetpos(file, &position) == 0)
				{
					size_t count = fread(_buffer, 1, _size, file);
					if(fsetpos(file, &position) == 0)
					{
						return count;
					}
				}
				return -1;
#elif defined VCZH_GCC
				long position = ftell(file);
				size_t count = fread(_buffer, 1, _size, file);
				fseek(file, position, SEEK_SET);
				return count;
#endif
			}
		};

/***********************************************************************
CreateOSFileStreamImpl
***********************************************************************/

		Ptr<IFileStreamImpl> CreateOSFileStreamImpl(const WString& fileName, FileStream::AccessRight accessRight)
		{
			return Ptr(new OSFileStreamImpl(fileName, accessRight));
		}

/***********************************************************************
FileStream
***********************************************************************/

		FileStream::FileStream(const WString& fileName, AccessRight _accessRight)
			: accessRight(_accessRight)
		{
			impl = filesystem::GetFileSystemImpl()->GetFileStreamImpl(fileName, _accessRight);
			if(!impl->Open())
			{
				impl = nullptr;
			}
		}

		FileStream::~FileStream()
		{
			Close();
		}

		bool FileStream::CanRead() const
		{
			return impl != nullptr && (accessRight == ReadOnly || accessRight == ReadWrite);
		}

		bool FileStream::CanWrite() const
		{
			return impl != nullptr && (accessRight == WriteOnly || accessRight == ReadWrite);
		}

		bool FileStream::CanSeek() const
		{
			return impl != nullptr;
		}

		bool FileStream::CanPeek() const
		{
			return impl != nullptr && (accessRight == ReadOnly || accessRight == ReadWrite);
		}

		bool FileStream::IsLimited() const
		{
			return impl != nullptr && accessRight == ReadOnly;
		}

		bool FileStream::IsAvailable() const
		{
			return impl != nullptr;
		}

		void FileStream::Close()
		{
			if(impl != nullptr)
			{
				impl->Close();
				impl = nullptr;
			}
		}

		pos_t FileStream::Position() const
		{
			return impl != nullptr ? impl->Position() : -1;
		}

		pos_t FileStream::Size() const
		{
			return impl != nullptr ? impl->Size() : -1;
		}

		void FileStream::Seek(pos_t _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::Seek(pos_t)#Stream is closed, cannot perform this operation.");
			impl->Seek(_size);
		}

		void FileStream::SeekFromBegin(pos_t _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::SeekFromBegin(pos_t)#Stream is closed, cannot perform this operation.");
			impl->SeekFromBegin(_size);
		}

		void FileStream::SeekFromEnd(pos_t _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::SeekFromEnd(pos_t)#Stream is closed, cannot perform this operation.");
			impl->SeekFromEnd(_size);
		}

		vint FileStream::Read(void* _buffer, vint _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::Read(pos_t)#Stream is closed, cannot perform this operation.");
			return impl->Read(_buffer, _size);
		}

		vint FileStream::Write(void* _buffer, vint _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::Write(pos_t)#Stream is closed, cannot perform this operation.");
			return impl->Write(_buffer, _size);
		}

		vint FileStream::Peek(void* _buffer, vint _size)
		{
			CHECK_ERROR(impl != nullptr, L"FileStream::Peek(pos_t)#Stream is closed, cannot perform this operation.");
			return impl->Peek(_buffer, _size);
		}
	}
}
