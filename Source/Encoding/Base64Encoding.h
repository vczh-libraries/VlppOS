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
		constexpr const vint Base64CycleBytes = 3;
		constexpr const vint Base64CycleChars = 4;

/***********************************************************************
Utf8Base64Encoder
***********************************************************************/

		class Utf8Base64Encoder : public EncoderBase
		{
		protected:
			uint8_t					cache[Base64CycleBytes];
			vint					cacheSize = 0;

			void					WriteBytesToCharArray(uint8_t* fromBytes, char8_t(&toChars)[Base64CycleChars], vint bytes);
			bool					WriteCycle(uint8_t*& reading, vint& _size);
			bool					WriteCache(uint8_t*& reading, vint& _size);
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
			uint8_t					cache[Base64CycleBytes];
			vint					cacheSize = 0;

			vint					ReadBytesFromCharArray(char8_t(&fromChars)[Base64CycleChars], uint8_t* toBytes);
			vint					ReadCycle(uint8_t*& writing, vint& _size);
			void					ReadCache(uint8_t*& writing, vint& _size);
		public:
			vint					Read(void* _buffer, vint _size) override;
		};
	}
}

#endif
