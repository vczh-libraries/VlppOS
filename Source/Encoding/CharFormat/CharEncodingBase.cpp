/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#include "CharEncodingBase.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
CharEncoderBase
***********************************************************************/

		void CharEncoderBase::Setup(IStream* _stream)
		{
			stream = _stream;
		}

		void CharEncoderBase::Close()
		{
		}

/***********************************************************************
CharDecoderBase
***********************************************************************/

		void CharDecoderBase::Setup(IStream* _stream)
		{
			stream = _stream;
		}

		void CharDecoderBase::Close()
		{
		}
	}
}
