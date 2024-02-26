/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_CHARFORMAT_CHARENCODINGBASE
#define VCZH_STREAM_ENCODING_CHARFORMAT_CHARENCODINGBASE

#include "../Encoding.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
CharEncoderBase and CharDecoderBase
***********************************************************************/

		/// <summary>Base type of all character encoder.</summary>
		class CharEncoderBase : public Object, public IEncoder
		{
		protected:
			IStream*						stream = nullptr;

		public:

			void							Setup(IStream* _stream) override;
			void							Close() override;
		};
		
		/// <summary>Base type of all character decoder.</summary>
		class CharDecoderBase : public Object, public IDecoder
		{
		protected:
			IStream*						stream = nullptr;

		public:

			void							Setup(IStream* _stream) override;
			void							Close() override;
		};
	}
}

#endif
