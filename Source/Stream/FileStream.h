/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_FILESTREAM
#define VCZH_STREAM_FILESTREAM

#include <stdio.h>
#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>A file stream. If the given file name is not working, the stream could be <b>unavailable</b>.</summary>
		class FileStream : public Object, public virtual IStream
		{
		public:
			/// <summary>Access to the file.</summary>
			enum AccessRight
			{
				/// <summary>The file is opened to read, making this stream <b>readable</b>,  <b>seekable</b> and <b>finite</b>.</summary>
				ReadOnly,
				/// <summary>The file is opened to write, making this stream <b>writable</b>.</summary>
				WriteOnly,
				/// <summary>The file is opened to both read and write, making this stream <b>readable</b>, <b>seekable</b> and <b>writable</b>.</summary>
				ReadWrite
			};
		protected:
			AccessRight				accessRight;
			FILE*					file;
		public:
			/// <summary>Create a file stream from a given file name.</summary>
			/// <param name="fileName">The file to operate.</param>
			/// <param name="_accessRight">Expected operations on the file.</param>
			FileStream(const WString& fileName, AccessRight _accessRight);
			~FileStream();

			bool					CanRead()const;
			bool					CanWrite()const;
			bool					CanSeek()const;
			bool					CanPeek()const;
			bool					IsLimited()const;
			bool					IsAvailable()const;
			void					Close();
			pos_t					Position()const;
			pos_t					Size()const;
			void					Seek(pos_t _size);
			void					SeekFromBegin(pos_t _size);
			void					SeekFromEnd(pos_t _size);
			vint					Read(void* _buffer, vint _size);
			vint					Write(void* _buffer, vint _size);
			vint					Peek(void* _buffer, vint _size);
		};
	}
}

#endif