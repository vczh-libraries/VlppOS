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
		public:
			vint					Write(void* _buffer, vint _size) override;
		};

/***********************************************************************
Utf8Base64Decoder
***********************************************************************/

		class Utf8Base64EDecoder : public DecoderBase
		{
		public:
			vint					Read(void* _buffer, vint _size) override;
		};
	}
}

#endif
