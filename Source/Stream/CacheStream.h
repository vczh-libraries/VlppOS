/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_CACHESTREAM
#define VCZH_STREAM_CACHESTREAM

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>
		/// <p>
		/// A potentially <b>readable</b>, <b>peekable</b>, <b>writable</b>, <b>seekable</b> and <b>finite</b> stream that creates on another stream.
		/// Each feature is available if the target stream has the same feature.
		/// </p>
		/// <p>
		/// When you read from the cache strema,
		/// it will read a specified size of content from the target stream at once and cache,
		/// reducing the number of operations on the target stream.
		/// </p>
		/// <p>
		/// When you write to the cache stream,
		/// it will cache all the data to write,
		/// and write to the target stream after the cache is full,
		/// reducing the number of operations on the target stream.
		/// </p>
		/// </summary>
		class CacheStream : public Object, public virtual IStream
		{
		protected:
			IStream*				target;
			vint					block;
			pos_t					start;
			pos_t					position;

			char*					buffer;
			vint					dirtyStart;
			vint					dirtyLength;
			vint					availableLength;
			pos_t					operatedSize;

			void					Flush();
			void					Load(pos_t _position);
			vint					InternalRead(void* _buffer, vint _size);
			vint					InternalWrite(void* _buffer, vint _size);
		public:
			/// <summary>Create a cache stream from a target stream.</summary>
			/// <param name="_target">The target stream.</param>
			/// <param name="_block">Size of the cache.</param>
			CacheStream(IStream& _target, vint _block=65536);
			~CacheStream();

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