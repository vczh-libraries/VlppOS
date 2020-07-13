/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_BROADCASTSTREAM
#define VCZH_STREAM_BROADCASTSTREAM

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>A <b>writable</b> stream that copy the written content to multiple output streams.</summary>
		/// <remarks>
		/// When writing happens, the boreadcast stream will only performance one write attempt to each output stream.
		/// </remarks>
		class BroadcastStream : public Object, public virtual IStream
		{
			typedef collections::List<IStream*>		StreamList;
		protected:
			bool					closed;
			pos_t					position;
			StreamList				streams;
		public:
			/// <summary>Create a boradcast stream.</summary>
			BroadcastStream();
			~BroadcastStream();

			/// <summary>
			/// Get the list of output streams.
			/// You can change this list to subscribe or unsubscribe.
			/// </summary>
			/// <returns>The list of output streams.</returns>
			StreamList&				Targets();
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