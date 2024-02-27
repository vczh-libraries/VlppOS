/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_ENCODING
#define VCZH_STREAM_ENCODING_ENCODING

#include "../Stream/Interfaces.h"

namespace vl
{
	namespace stream
	{
/***********************************************************************
IEncoder and IDecoder
***********************************************************************/

		/// <summary>Encoder interface. This interface defines a writable transformation from one stream to another stream. You can create a [T:vl.stream.EncoderStream] after you have an encoder.</summary>
		class IEncoder : public Interface
		{
		public:
			/// <summary>Set a target <b>writable</b> stream to receive data. <see cref="Write"/> transforms the content and write to this tream.</summary>
			/// <param name="_stream">The target <b>writable</b> stream.</param>
			virtual void					Setup(IStream* _stream)=0;
			/// <summary>Stop the transformation, ensuring all content is written to the target stream.</summary>
			virtual	void					Close()=0;
			/// <summary>
			/// Transform content and write to the target stream.
			/// This function could use caching to improve performance.
			/// Please do not expect that all transformed content will be written to the target stream immediately.
			/// </summary>
			/// <returns>
			/// Returns the actual size of the content that has written <b>before</b> transforming.
			/// A successful write operation may only cache the data without actually write anything to the target stream.
			/// </returns>
			/// <param name="_buffer">A buffer storing the content to transform.</param>
			/// <param name="_size">The expected size of the content in bytes in "_buffer" to use.</param>
			virtual vint					Write(void* _buffer, vint _size)=0;
		};
		
		/// <summary>Decoder interface. This interface defines a readable transformation from one stream to another stream. You can create a [T:vl.stream.DecoderStream] after you have an decoder.</summary>
		class IDecoder : public Interface
		{
		public:
			/// <summary>
			/// Set a target <b>readable</b> stream.
			/// <see cref="Read"/> reads from this tream and transform the content.
			/// </summary>
			/// <param name="_stream">The target <b>readable</b> stream.</param>
			virtual void					Setup(IStream* _stream)=0;
			/// <summary>Stop the transformation.</summary>
			virtual	void					Close()=0;
			/// <summary>Read from the target stream and transform the content.</summary>
			/// <returns>Returns the actual size of the content has read after transforming.</returns>
			/// <param name="_buffer">A buffer to store the content.</param>
			/// <param name="_size">The expected size of the content in bytes in "_buffer" to receive.</param>
			virtual vint					Read(void* _buffer, vint _size)=0;
		};

/***********************************************************************
EncoderBase and DecoderBase
***********************************************************************/

		/// <summary>Basic implementation of IEncoder.</summary>
		class EncoderBase : public Object, public IEncoder
		{
		protected:
			IStream*						stream = nullptr;

		public:

			void							Setup(IStream* _stream) override;
			void							Close() override;
		};
		
		/// <summary>Basic implementation of IDecoder.</summary>
		class DecoderBase : public Object, public IDecoder
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