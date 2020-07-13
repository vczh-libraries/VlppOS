/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_RECORDERSTREAM
#define VCZH_STREAM_RECORDERSTREAM

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{
		/// <summary>
		/// A readable stream that, reads from one stream, and copy everything that is read to another stream.
		/// The stream is <b>unavailable</b> if one of the input stream or the output stream is <b>unavailable</b>.
		/// The stream is <b>readable</b>, and potentially <b>finite</b>.
		/// </summary>
		/// <remarks>
		/// When reading happens, the recorder stream will only performance one write attempt to the output stream.
		/// </remarks>
		class RecorderStream : public Object, public virtual IStream
		{
		protected:
			IStream*				in;
			IStream*				out;
		public:
			/// <summary>Create a recorder stream.</summary>
			/// <param name="_in">
			/// The input stream.
			/// This recorder stream is <b>readable</b> only when the input stream is <b>readable</b>
			/// This recorder stream is <b>finite</b> only when the input stream is <b>finite</b>
			/// </param>
			/// <param name="_out">
			/// The output stream.
			/// </param>
			RecorderStream(IStream& _in, IStream& _out);
			~RecorderStream();

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