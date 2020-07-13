/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_MEMORYSTREAM
#define VCZH_STREAM_MEMORYSTREAM

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>A <b>readable</b>, <b>peekable</b>, <b>writable</b> and <b>seekable</b> stream that creates on a buffer.</summary>
		class MemoryStream : public Object, public virtual IStream
		{
		protected:
			vint					block;
			char*					buffer;
			vint					size;
			vint					position;
			vint					capacity;

			void					PrepareSpace(vint totalSpace);
		public:
			/// <summary>Create a memory stream.</summary>
			/// <param name="_block">
			/// Size for each allocation.
			/// When the allocated buffer is not big enough for writing,
			/// the buffer will be rebuilt with an extension of "_block" in bytes.
			/// </param>
			MemoryStream(vint _block=65536);
			~MemoryStream();

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
			void*					GetInternalBuffer();
		};
	}
}

#endif