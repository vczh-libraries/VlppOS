/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_CHARFORMAT_MBCSENCODING
#define VCZH_STREAM_ENCODING_CHARFORMAT_MBCSENCODING

#include "../Encoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
MbcsEncoder
***********************************************************************/

		/// <summary>Encoder to write text in the local code page.</summary>
		class MbcsEncoder : public EncoderBase
		{
		protected:
			vuint8_t						cacheBuffer[sizeof(char32_t)];
			vint							cacheSize = 0;

			vint							WriteString(wchar_t* _buffer, vint chars);
		public:

			vint							Write(void* _buffer, vint _size) override;
		};

/***********************************************************************
MbcsDecoder
***********************************************************************/

		/// <summary>Decoder to read text in the local code page.</summary>
		class MbcsDecoder : public DecoderBase
		{
		protected:
			vuint8_t						cacheBuffer[sizeof(wchar_t)];
			vint							cacheSize = 0;

			vint							ReadString(wchar_t* _buffer, vint chars);
		public:

			vint							Read(void* _buffer, vint _size) override;
		};
	}
}

#endif
