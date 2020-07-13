/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_MEMORYWRAPPERSTREAM
#define VCZH_STREAM_MEMORYWRAPPERSTREAM

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>A <b>readable</b>, <b>peekable</b>, <b>writable</b>, <b>seekable</b> and <b>finite</b> stream that creates on a buffer.</summary>
		class MemoryWrapperStream : public Object, public virtual IStream
		{
		protected:
			char*					buffer;
			vint						size;
			vint						position;
		public:
			/// <summary>Create a memory wrapper stream.</summary>
			/// <param name="_buffer">The buffer to operate.</param>
			/// <param name="_size">Size of the buffer in bytes.</param>
			MemoryWrapperStream(void* _buffer, vint _size);
			~MemoryWrapperStream();

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