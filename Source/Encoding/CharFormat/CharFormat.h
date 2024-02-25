/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_ENCODING_CHARFORMAT
#define VCZH_STREAM_ENCODING_CHARFORMAT

#include "../Encoding.h"

namespace vl
{
	namespace encoding
	{

/***********************************************************************
Helper Functions
***********************************************************************/
	}

	namespace stream
	{

/***********************************************************************
UtfStreamConsumer<T>
***********************************************************************/

		template<typename T>
		class UtfStreamConsumer : public Object
		{
		protected:
			IStream*				stream = nullptr;

			T Consume()
			{
				T c;
				vint size = stream->Read(&c, sizeof(c));
				if (size != sizeof(c)) return 0;
				return c;
			}
		public:
			void Setup(IStream* _stream)
			{
				stream = _stream;
			}

			bool HasIllegalChar() const
			{
				return false;
			}
		};

/***********************************************************************
UtfStreamToStreamReader<TFrom, TTo>
***********************************************************************/

		template<typename TFrom, typename TTo>
		class UtfStreamToStreamReader : public encoding::UtfToUtfReaderBase<TFrom, TTo, UtfStreamConsumer<TFrom>>
		{
		public:
			void Setup(IStream* _stream)
			{
				this->internalReader.Setup(_stream);
			}

			encoding::UtfCharCluster SourceCluster() const
			{
				return this->internalReader.SourceCluster();
			}
		};

		template<typename TFrom, typename TTo>
			requires(std::is_same_v<TFrom, char32_t> || std::is_same_v<TTo, char32_t>)
		class UtfStreamToStreamReader<TFrom, TTo> : public encoding::UtfToUtfReaderBase<TFrom, TTo, UtfStreamConsumer<TFrom>>
		{
		};

/***********************************************************************
Char Encoder and Decoder
***********************************************************************/

		/// <summary>Base type of all character encoder.</summary>
		class CharEncoder : public Object, public IEncoder
		{
		protected:
			IStream*						stream = nullptr;
			vuint8_t						cacheBuffer[sizeof(char32_t)];
			vint							cacheSize = 0;

			virtual vint					WriteString(wchar_t* _buffer, vint chars) = 0;
		public:

			void							Setup(IStream* _stream);
			void							Close();
			vint							Write(void* _buffer, vint _size);
		};
		
		/// <summary>Base type of all character decoder.</summary>
		class CharDecoder : public Object, public IDecoder
		{
		protected:
			IStream*						stream = nullptr;
			vuint8_t						cacheBuffer[sizeof(wchar_t)];
			vint							cacheSize = 0;

			virtual vint					ReadString(wchar_t* _buffer, vint chars) = 0;
		public:

			void							Setup(IStream* _stream);
			void							Close();
			vint							Read(void* _buffer, vint _size);
		};

/***********************************************************************
Mbcs
***********************************************************************/
		
		/// <summary>Encoder to write text in the local code page.</summary>
		class MbcsEncoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars) override;
		};
		
		/// <summary>Decoder to read text in the local code page.</summary>
		class MbcsDecoder : public CharDecoder
		{
		protected:
			vint							ReadString(wchar_t* _buffer, vint chars) override;
		};

/***********************************************************************
Unicode General
***********************************************************************/

		template<typename T>
		class UtfGeneralEncoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars) override;
		};

		extern template class UtfGeneralEncoder<char8_t>;
		extern template class UtfGeneralEncoder<char16_t>;
		extern template class UtfGeneralEncoder<char16be_t>;
		extern template class UtfGeneralEncoder<char32_t>;

		template<typename T>
		class UtfGeneralDecoder : public CharDecoder
		{
		protected:
			UtfStreamToStreamReader<T, wchar_t>		reader;

			vint							ReadString(wchar_t* _buffer, vint chars) override;
		};

		extern template class UtfGeneralDecoder<char8_t>;
		extern template class UtfGeneralDecoder<char16_t>;
		extern template class UtfGeneralDecoder<char16be_t>;
		extern template class UtfGeneralDecoder<char32_t>;

/***********************************************************************
Unicode General (wchar_t)
***********************************************************************/

		template<>
		class UtfGeneralEncoder<wchar_t> : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars) override;
		};

		template<>
		class UtfGeneralDecoder<wchar_t> : public CharDecoder
		{
		protected:
			vint							ReadString(wchar_t* _buffer, vint chars) override;
		};

/***********************************************************************
Utf-8
***********************************************************************/
		
#if defined VCZH_MSVC
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars) override;
		};
#elif defined VCZH_GCC
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public UtfGeneralEncoder<char8_t> {};
#endif
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf8Decoder : public UtfGeneralDecoder<char8_t> {};

/***********************************************************************
Utf-16 / Utf-16BE / Utf-32
***********************************************************************/

		/// <summary>Encoder to write big endian UTF-16 to.</summary>
		class Utf16BEEncoder : public UtfGeneralEncoder<char16be_t> {};
		/// <summary>Decoder to read big endian UTF-16 text.</summary>
		class Utf16BEDecoder : public UtfGeneralDecoder<char16be_t> {};

#if defined VCZH_WCHAR_UTF16
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public UtfGeneralEncoder<wchar_t> {};
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public UtfGeneralDecoder<wchar_t> {};
		
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public UtfGeneralEncoder<char32_t> {};
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public UtfGeneralDecoder<char32_t> {};

#elif defined VCZH_WCHAR_UTF32
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public UtfGeneralEncoder<char16_t> {};
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public UtfGeneralDecoder<char16_t> {};

		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public UtfGeneralEncoder<wchar_t> {};
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public UtfGeneralDecoder<wchar_t> {};

#endif

/***********************************************************************
Bom
***********************************************************************/
		
		/// <summary>Encoder to write text in a specified encoding. A BOM will be added at the beginning.</summary>
		class BomEncoder : public Object, public IEncoder
		{
		public:
			/// <summary>Text encoding.</summary>
			enum Encoding
			{
				/// <summary>Multi-bytes character string.</summary>
				Mbcs,
				/// <summary>UTF-8. EF, BB, BF will be written before writing any text.</summary>
				Utf8,
				/// <summary>UTF-16. FF FE will be written before writing any text.</summary>
				Utf16,
				/// <summary>Big endian UTF-16. FE FF, BF will be written before writing any text.</summary>
				Utf16BE
			};
		protected:
			Encoding						encoding;
			IEncoder*						encoder;
		public:
			/// <summary>Create an encoder with a specified encoding.</summary>
			/// <param name="_encoding">The specified encoding.</param>
			BomEncoder(Encoding _encoding);
			~BomEncoder();

			void							Setup(IStream* _stream);
			void							Close();
			vint							Write(void* _buffer, vint _size);
		};
		
		/// <summary>Decoder to read text. This decoder depends on BOM at the beginning to decide the format of the input.</summary>
		class BomDecoder : public Object, public IDecoder
		{
		private:
			class BomStream : public Object, public IStream
			{
			protected:
				IStream*					stream;
				char						bom[3];
				vint						bomLength;
				vint						bomPosition;
			public:
				BomStream(IStream* _stream, char* _bom, vint _bomLength);

				bool						CanRead()const;
				bool						CanWrite()const;
				bool						CanSeek()const;
				bool						CanPeek()const;
				bool						IsLimited()const;
				bool						IsAvailable()const;
				void						Close();
				pos_t						Position()const;
				pos_t						Size()const;
				void						Seek(pos_t _size);
				void						SeekFromBegin(pos_t _size);
				void						SeekFromEnd(pos_t _size);
				vint						Read(void* _buffer, vint _size);
				vint						Write(void* _buffer, vint _size);
				vint						Peek(void* _buffer, vint _size);
			};
		protected:
			IDecoder*						decoder;
			IStream*						stream;

		public:
			/// <summary>Create an decoder, BOM will be consumed before reading any text.</summary>
			BomDecoder();
			~BomDecoder();

			void							Setup(IStream* _stream);
			void							Close();
			vint							Read(void* _buffer, vint _size);
		};

/***********************************************************************
Encoding Test
***********************************************************************/

		/// <summary>Guess the text encoding in a buffer.</summary>
		/// <param name="buffer">The buffer to guess.</param>
		/// <param name="size">Size of the buffer in bytes.</param>
		/// <param name="encoding">Returns the most possible encoding.</param>
		/// <param name="containsBom">Returns true if the BOM information is at the beginning of the buffer.</param>
		extern void							TestEncoding(unsigned char* buffer, vint size, BomEncoder::Encoding& encoding, bool& containsBom);
	}
}

#endif
