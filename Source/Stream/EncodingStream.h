/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODINGSTREAM
#define VCZH_STREAM_ENCODINGSTREAM

#include "../Encoding/Encoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
Encoding Related
***********************************************************************/

		/// <summary>Encoder stream, a <b>writable</b> and potentially <b>finite</b> stream using [T:vl.stream.IEncoder] to transform content.</summary>
		class EncoderStream : public virtual IStream
		{
		protected:
			IStream*					stream;
			IEncoder*					encoder;
			pos_t						position;

		public:
			/// <summary>Create en encoder stream.</summary>
			/// <param name="_stream">The output stream to write.</param>
			/// <param name="_encoder">The encoder to transform content.</param>
			EncoderStream(IStream& _stream, IEncoder& _encoder);
			~EncoderStream();

			bool						CanRead()const;
			bool						CanWrite()const;
			bool						CanSeek()const;
			bool						CanPeek()const;
			bool						IsLimited()const;
			bool						IsAvailable()const;
			void						Close();
			pos_t						Position()const;
			pos_t						Size()const;
			void						Seek(pos_t _size);
			void						SeekFromBegin(pos_t _size);
			void						SeekFromEnd(pos_t _size);
			vint						Read(void* _buffer, vint _size);
			vint						Write(void* _buffer, vint _size);
			vint						Peek(void* _buffer, vint _size);
		};
		
		/// <summary>Decoder stream, a <b>readable</b> and potentially <b>finite</b> stream using [T:vl.stream.IDecoder] to transform content.</summary>
		class DecoderStream : public virtual IStream
		{
		protected:
			IStream*					stream;
			IDecoder*					decoder;
			pos_t						position;

		public:
			/// <summary>Create a decoder stream.</summary>
			/// <param name="_stream">The input stream to read.</param>
			/// <param name="_decoder">The decoder to transform content.</param>
			DecoderStream(IStream& _stream, IDecoder& _decoder);
			~DecoderStream();

			bool						CanRead()const;
			bool						CanWrite()const;
			bool						CanSeek()const;
			bool						CanPeek()const;
			bool						IsLimited()const;
			bool						IsAvailable()const;
			void						Close();
			pos_t						Position()const;
			pos_t						Size()const;
			void						Seek(pos_t _size);
			void						SeekFromBegin(pos_t _size);
			void						SeekFromEnd(pos_t _size);
			vint						Read(void* _buffer, vint _size);
			vint						Write(void* _buffer, vint _size);
			vint						Peek(void* _buffer, vint _size);
		};
	}
}

#endif