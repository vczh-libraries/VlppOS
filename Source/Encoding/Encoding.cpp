/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "Encoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
EncoderBase
***********************************************************************/

		void EncoderBase::Setup(IStream* _stream)
		{
			stream = _stream;
		}

		void EncoderBase::Close()
		{
		}

/***********************************************************************
DecoderBase
***********************************************************************/

		void DecoderBase::Setup(IStream* _stream)
		{
			stream = _stream;
		}

		void DecoderBase::Close()
		{
		}
	}
}
