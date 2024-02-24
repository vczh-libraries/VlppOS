/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_CHARFORMAT
#define VCZH_STREAM_CHARFORMAT

#include "Interfaces.h"

namespace vl
{
	namespace encoding
	{

/***********************************************************************
Helper Functions
***********************************************************************/

		template<typename T>
		void SwapBytesForUtf16BE(T* _buffer, vint chars)
		{
			static_assert(sizeof(T) == sizeof(char16_t));
			for (vint i = 0; i < chars; i++)
			{
				SwapByteForUtf16BE(_buffer[i]);
			}
		}
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
		class UtfStreamToStreamReader : public encoding::UtfFrom32ReaderBase<TTo, encoding::UtfReaderConsumer<encoding::UtfTo32ReaderBase<TFrom, UtfStreamConsumer<TFrom>>>>
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

		template<typename TTo>
		class UtfStreamToStreamReader<char32_t, TTo> : public encoding::UtfFrom32ReaderBase<TTo, UtfStreamConsumer<char32_t>>
		{
		};

		template<typename TFrom>
		class UtfStreamToStreamReader<TFrom, char32_t> : public encoding::UtfTo32ReaderBase<TFrom, UtfStreamConsumer<TFrom>>
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

			virtual vint					WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate) = 0;
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
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
		
		/// <summary>Decoder to read text in the local code page.</summary>
		class MbcsDecoder : public CharDecoder
		{
		protected:
			vint							ReadString(wchar_t* _buffer, vint chars);
		};

/***********************************************************************
Unicode General
***********************************************************************/

		template<typename T>
		class UtfGeneralEncoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};

		extern template class UtfGeneralEncoder<char8_t>;
		extern template class UtfGeneralEncoder<char16_t>;
		extern template class UtfGeneralEncoder<char32_t>;

		template<typename T>
		class UtfGeneralDecoder : public CharDecoder
		{
		protected:
			UtfStreamToStreamReader<T, wchar_t>		reader;

			vint							ReadString(wchar_t* _buffer, vint chars);
		};

		extern template class UtfGeneralDecoder<char8_t>;
		extern template class UtfGeneralDecoder<char16_t>;
		extern template class UtfGeneralDecoder<char32_t>;

/***********************************************************************
Utf-8
***********************************************************************/
		
#if defined VCZH_MSVC
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
#elif define VCZH_GCC

		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public UtfGeneralEncoder<char8_t> {};
#endif
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf8Decoder : public UtfGeneralDecoder<char8_t> {};

#if defined VCZH_WCHAR_UTF16

/***********************************************************************
Utf-16
***********************************************************************/
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
		
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public CharDecoder
		{
		protected:
			vint							ReadString(wchar_t* _buffer, vint chars);
		};

/***********************************************************************
Utf-16-be
***********************************************************************/
		
		/// <summary>Encoder to write big endian UTF-16 to.</summary>
		class Utf16BEEncoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
		
		/// <summary>Decoder to read big endian UTF-16 text.</summary>
		class Utf16BEDecoder : public CharDecoder
		{
		protected:
			UtfStreamToStreamReader<char16be_t, wchar_t>	reader;

			vint							ReadString(wchar_t* _buffer, vint chars);
		};

/***********************************************************************
Utf-32
***********************************************************************/
		
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public UtfGeneralEncoder<char32_t> {};
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public UtfGeneralDecoder<char32_t> {};

#elif defined VCZH_WCHAR_UTF32

/***********************************************************************
Utf-16
***********************************************************************/
		
		/// <summary>Encoder to write UTF-16 text.</summary>
		class Utf16Encoder : public UtfGeneralEncoder<char16_t> {};
		
		/// <summary>Decoder to read UTF-16 text.</summary>
		class Utf16Decoder : public UtfGeneralDecoder<char16_t> {};

/***********************************************************************
Utf-16-be
***********************************************************************/
		
		/// <summary>Encoder to write big endian UTF-16 to.</summary>
		class Utf16BEEncoder : public UtfGeneralEncoder<char16be_t> {};
		
		/// <summary>Decoder to read big endian UTF-16 text.</summary>
		class Utf16BEDecoder : public UtfGeneralDecoder<char16be_t> {};

/***********************************************************************
Utf-32
***********************************************************************/
		
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf32Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf32Decoder : public CharDecoder
		{
		protected:
#if defined VCZH_WCHAR_UTF16
			UtfStreamToStreamReader<char32_t, wchar_t>		reader;
#endif

			vint							ReadString(wchar_t* _buffer, vint chars);
		public:
		};
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
