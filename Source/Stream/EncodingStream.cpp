/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "EncodingStream.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
EncoderStream
***********************************************************************/

		EncoderStream::EncoderStream(IStream& _stream, IEncoder& _encoder)
			:stream(&_stream)
			,encoder(&_encoder)
			,position(0)
		{
			encoder->Setup(stream);
		}

		EncoderStream::~EncoderStream()
		{
			Close();
		}

		bool EncoderStream::CanRead()const
		{
			return false;
		}

		bool EncoderStream::CanWrite()const
		{
			return IsAvailable();
		}

		bool EncoderStream::CanSeek()const
		{
			return false;
		}

		bool EncoderStream::CanPeek()const
		{
			return false;
		}

		bool EncoderStream::IsLimited()const
		{
			return stream!=0 && stream->IsLimited();
		}

		bool EncoderStream::IsAvailable()const
		{
			return stream!=0 && stream->IsAvailable();
		}

		void EncoderStream::Close()
		{
			encoder->Close();
			stream=0;
		}

		pos_t EncoderStream::Position()const
		{
			return IsAvailable()?position:-1;
		}

		pos_t EncoderStream::Size()const
		{
			return -1;
		}

		void EncoderStream::Seek(pos_t _size)
		{
			CHECK_FAIL(L"EncoderStream::Seek(pos_t)#Operation not supported.");
		}

		void EncoderStream::SeekFromBegin(pos_t _size)
		{
			CHECK_FAIL(L"EncoderStream::SeekFromBegin(pos_t)#Operation not supported.");
		}

		void EncoderStream::SeekFromEnd(pos_t _size)
		{
			CHECK_FAIL(L"EncoderStream::SeekFromEnd(pos_t)#Operation not supported.");
		}

		vint EncoderStream::Read(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"EncoderStream::Read(void*, vint)#Operation not supported.");
		}

		vint EncoderStream::Write(void* _buffer, vint _size)
		{
			vint result=encoder->Write(_buffer, _size);
			if(result>=0)
			{
				position+=result;
			}
			return result;
		}

		vint EncoderStream::Peek(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"EncoderStream::Peek(void*, vint)#Operation not supported.");
		}

/***********************************************************************
DecoderStream
***********************************************************************/

		DecoderStream::DecoderStream(IStream& _stream, IDecoder& _decoder)
			:stream(&_stream)
			,decoder(&_decoder)
			,position(0)
		{
			decoder->Setup(stream);
		}

		DecoderStream::~DecoderStream()
		{
			Close();
		}

		bool DecoderStream::CanRead()const
		{
			return IsAvailable();
		}

		bool DecoderStream::CanWrite()const
		{
			return false;
		}

		bool DecoderStream::CanSeek()const
		{
			return false;
		}

		bool DecoderStream::CanPeek()const
		{
			return false;
		}

		bool DecoderStream::IsLimited()const
		{
			return stream!=0 && stream->IsLimited();
		}

		bool DecoderStream::IsAvailable()const
		{
			return stream!=0 && stream->IsAvailable();
		}

		void DecoderStream::Close()
		{
			decoder->Close();
			stream=0;
		}

		pos_t DecoderStream::Position()const
		{
			return IsAvailable()?position:-1;
		}

		pos_t DecoderStream::Size()const
		{
			return -1;
		}

		void DecoderStream::Seek(pos_t _size)
		{
			CHECK_FAIL(L"DecoderStream::Seek(pos_t)#Operation not supported.");
		}

		void DecoderStream::SeekFromBegin(pos_t _size)
		{
			CHECK_FAIL(L"DecoderStream::SeekFromBegin(pos_t)#Operation not supported.");
		}

		void DecoderStream::SeekFromEnd(pos_t _size)
		{
			CHECK_FAIL(L"DecoderStream::SeekFromEnd(pos_t)#Operation not supported.");
		}

		vint DecoderStream::Read(void* _buffer, vint _size)
		{
			vint result=decoder->Read(_buffer, _size);
			if(result>=0)
			{
				position+=result;
			}
			return result;
		}

		vint DecoderStream::Write(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"DecoderStream::Write(void*, vint)#Operation not supported.");
		}

		vint DecoderStream::Peek(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"DecoderStream::Peek(void*, vint)#Operation not supported.");
		}
	}
}
