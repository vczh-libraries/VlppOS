/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "BomEncoding.h"
#include "CharFormat.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
BomEncoder
***********************************************************************/

		BomEncoder::BomEncoder(Encoding _encoding)
			:encoding(_encoding)
			,encoder(0)
		{
			switch(encoding)
			{
			case Mbcs:
				encoder=new MbcsEncoder;
				break;
			case Utf8:
				encoder=new Utf8Encoder;
				break;
			case Utf16:
				encoder=new Utf16Encoder;
				break;
			case Utf16BE:
				encoder=new Utf16BEEncoder;
				break;
			}
		}

		BomEncoder::~BomEncoder()
		{
			Close();
		}

		void BomEncoder::Setup(IStream* _stream)
		{
			switch(encoding)
			{
			case Mbcs:
				break;
			case Utf8:
				_stream->Write((void*)"\xEF\xBB\xBF", 3);
				break;
			case Utf16:
				_stream->Write((void*)"\xFF\xFE", 2);
				break;
			case Utf16BE:
				_stream->Write((void*)"\xFE\xFF", 2);
				break;
			}
			encoder->Setup(_stream);
		}

		void BomEncoder::Close()
		{
			if(encoder)
			{
				encoder->Close();
				delete encoder;
				encoder=0;
			}
		}

		vint BomEncoder::Write(void* _buffer, vint _size)
		{
			return encoder->Write(_buffer, _size);
		}

/***********************************************************************
BomDecoder
***********************************************************************/

		BomDecoder::BomStream::BomStream(IStream* _stream, char* _bom, vint _bomLength)
			:stream(_stream)
			,bomPosition(0)
			,bomLength(_bomLength)
		{
			memcpy(bom, _bom, bomLength);
		}

		bool BomDecoder::BomStream::CanRead()const
		{
			return IsAvailable();
		}

		bool BomDecoder::BomStream::CanWrite()const
		{
			return false;
		}

		bool BomDecoder::BomStream::CanSeek()const
		{
			return false;
		}

		bool BomDecoder::BomStream::CanPeek()const
		{
			return false;
		}

		bool BomDecoder::BomStream::IsLimited()const
		{
			return stream!=0 && stream->IsLimited();
		}

		bool BomDecoder::BomStream::IsAvailable()const
		{
			return stream!=0 && stream->IsAvailable();
		}

		void BomDecoder::BomStream::Close()
		{
			stream=0;
		}

		pos_t BomDecoder::BomStream::Position()const
		{
			return IsAvailable()?bomPosition+stream->Position():-1;
		}

		pos_t BomDecoder::BomStream::Size()const
		{
			return -1;
		}

		void BomDecoder::BomStream::Seek(pos_t _size)
		{
			CHECK_FAIL(L"BomDecoder::BomStream::Seek(pos_t)#Operation not supported.");
		}

		void BomDecoder::BomStream::SeekFromBegin(pos_t _size)
		{
			CHECK_FAIL(L"BomDecoder::BomStream::SeekFromBegin(pos_t)#Operation not supported.");
		}

		void BomDecoder::BomStream::SeekFromEnd(pos_t _size)
		{
			CHECK_FAIL(L"BomDecoder::BomStream::SeekFromEnd(pos_t)#Operation not supported.");
		}

		vint BomDecoder::BomStream::Read(void* _buffer, vint _size)
		{
			vint result=0;
			unsigned char* buffer=(unsigned char*)_buffer;
			if(bomPosition<bomLength)
			{
				vint remain=bomLength-bomPosition;
				result=remain<_size?remain:_size;
				memcpy(buffer, bom+bomPosition, result);
				buffer+=result;
				bomPosition+=result;
				_size-=result;
			}
			if(_size)
			{
				result+=stream->Read(buffer, _size);
			}
			return result;
		}

		vint BomDecoder::BomStream::Write(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"BomDecoder::BomStream::Write(void*, vint)#Operation not supported.");
		}

		vint BomDecoder::BomStream::Peek(void* _buffer, vint _size)
		{
			CHECK_FAIL(L"BomDecoder::BomStream::Peek(void*, vint)#Operation not supported.");
		}

		BomDecoder::BomDecoder()
			:decoder(0)
		{
		}

		BomDecoder::~BomDecoder()
		{
			Close();
		}

		void BomDecoder::Setup(IStream* _stream)
		{
			char bom[3]={0};
			vint length=_stream->Read(bom, sizeof(bom));
			if(strncmp(bom, "\xEF\xBB\xBF", 3)==0)
			{
				decoder=new Utf8Decoder;
				stream=new BomStream(_stream, bom+3, 0);
			}
			else if(strncmp(bom, "\xFF\xFE", 2)==0)
			{
				decoder=new Utf16Decoder;
				stream=new BomStream(_stream, bom+2, 1);
			}
			else if(strncmp(bom, "\xFE\xFF", 2)==0)
			{
				decoder=new Utf16BEDecoder;
				stream=new BomStream(_stream, bom+2, 1);
			}
			else
			{
				decoder=new MbcsDecoder;
				stream=new BomStream(_stream, bom, 3);
			}
			decoder->Setup(stream);
		}

		void BomDecoder::Close()
		{
			if(decoder)
			{
				decoder->Close();
				delete decoder;
				decoder=0;
				stream->Close();
				delete stream;
				stream=0;
			}
		}

		vint BomDecoder::Read(void* _buffer, vint _size)
		{
			return decoder->Read(_buffer, _size);
		}
	}
}
