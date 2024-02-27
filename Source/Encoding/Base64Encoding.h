/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_BASE64ENCODING
#define VCZH_STREAM_ENCODING_BASE64ENCODING

#include "Encoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
Utf8Base64Encoder
***********************************************************************/

		class Utf8Base64Encoder : public EncoderBase
		{
		protected:
			uint8_t					cache[3];
			vint					cacheSize = 0;

			void					WriteBytes(uint8_t* fromBytes, char8_t(&toChars)[4], vint bytes);
		public:
			vint					Write(void* _buffer, vint _size) override;
			void					Close() override;
		};

/***********************************************************************
Utf8Base64Decoder
***********************************************************************/

		class Utf8Base64Decoder : public DecoderBase
		{
		protected:
			uint8_t					cache[3];
			vint					cacheSize = 0;

			vint					ReadBytes(char8_t(&fromChars)[4], uint8_t* toBytes);
			vint					ReadCycle(uint8_t*& writing, vint& _size);
			void					ReadCache(uint8_t*& writing, vint& _size);
		public:
			vint					Read(void* _buffer, vint _size) override;
		};
	}
}

#endif
