/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_CHARFORMAT
#define VCZH_STREAM_CHARFORMAT

#include "Interfaces.h"

namespace vl
{
	struct char16be_t
	{
		char16_t				value;
	};

	namespace encoding
	{

/***********************************************************************
Helper Functions
***********************************************************************/

		template<typename T>
		__forceinline void SwapByteForUtf16BE(T& c)
		{
			static_assert(sizeof(T) == sizeof(char16_t));
			vuint8_t* bytes = (vuint8_t*)&c;
			vuint8_t t = bytes[0];
			bytes[0] = bytes[1];
			bytes[1] = t;
		}

		template<typename T>
		void SwapBytesForUtf16BE(T* _buffer, vint chars)
		{
			static_assert(sizeof(T) == sizeof(char16_t));
			for (vint i = 0; i < chars; i++)
			{
				SwapByteForUtf16BE(_buffer[i]);
			}
		}

/***********************************************************************
char16be_t
***********************************************************************/

		template<>
		struct UtfConversion<char16be_t>
		{
			static const vint		BufferLength = 2;

			static vint From32(char32_t source, char16be_t(&dest)[BufferLength])
			{
				char16_t destle[BufferLength];
				UtfConversion<char16_t>::From32(source, destle);
				SwapByteForUtf16BE(destle[0]);
				SwapByteForUtf16BE(destle[1]);
				dest[0].value = destle[0];
				dest[1].value = destle[1];
			}

			static vint To32(const char16be_t* source, vint sourceLength, char32_t& dest)
			{
				char16_t destle[BufferLength];
				if (sourceLength >= 1) destle[0] = source[0].value;
				if (sourceLength >= 2) destle[1] = source[1].value;
				SwapByteForUtf16BE(destle[0]);
				SwapByteForUtf16BE(destle[1]);
				UtfConversion<char16_t>::To32(destle, sourceLength, dest);
			}
		};

/***********************************************************************
UtfStringRangeConsumer<T>
***********************************************************************/

		template<typename T>
		class UtfStringRangeConsumer : public Object
		{
		protected:
			const T*				starting = nullptr;
			const T*				ending = nullptr;
			const T*				consuming = nullptr;

			T Consume()
			{
				T c = *consuming;
				if (c) consuming++;
				return c;
			}
		public:
			UtfStringRangeConsumer(const T* _starting, const T* _ending)
				: starting(_starting)
				, ending(_ending)
				, consuming(_starting)
			{
			}

			UtfStringRangeConsumer(const T* _starting, vint count)
				: starting(_starting)
				, ending(_starting + count)
				, consuming(_starting)
			{
			}

			bool HasIllegalChar() const
			{
				return false;
			}
		};

/***********************************************************************
UtfStringRangeToStringRangeReader<TFrom, TTo>
***********************************************************************/

		template<typename TFrom, typename TTo>
		class UtfStringRangeToStringRangeReader : public UtfFrom32ReaderBase<TTo, UtfStringRangeConsumer<UtfTo32ReaderBase<TFrom, UtfStringRangeConsumer<TFrom>>>>
		{
			using TBase = UtfFrom32ReaderBase<TTo, UtfStringRangeConsumer<UtfTo32ReaderBase<TFrom, UtfStringRangeConsumer<TFrom>>>>;
		public:
			UtfStringRangeToStringRangeReader(const TFrom* _starting, const TFrom* _ending)
				: TBase(_starting, _ending)
			{
			}

			UtfStringRangeToStringRangeReader(const TFrom* _starting, vint count)
				: TBase(_starting, count)
			{
			}

			UtfCharCluster SourceCluster() const
			{
				return this->internalReader.SourceCluster();
			}
		};

		template<typename TTo>
		class UtfStringRangeToStringRangeReader<char32_t, TTo> : public UtfFrom32ReaderBase<TTo, UtfStringRangeConsumer<char32_t>>
		{
			using TBase = UtfFrom32ReaderBase<TTo, UtfStringRangeConsumer<char32_t>>;
		public:
			UtfStringRangeToStringRangeReader(const char32_t* _starting, const char32_t* _ending)
				: TBase(_starting, _ending)
			{
			}

			UtfStringRangeToStringRangeReader(const char32_t* _starting, vint count)
				: TBase(_starting, count)
			{
			}
		};

		template<typename TFrom>
		class UtfStringRangeToStringRangeReader<TFrom, char32_t> : public UtfTo32ReaderBase<TFrom, UtfStringRangeConsumer<TFrom>>
		{
			using TBase = UtfTo32ReaderBase<TFrom, UtfStringRangeConsumer<TFrom>>;
		public:
			UtfStringRangeToStringRangeReader(const TFrom* _starting, const TFrom* _ending)
				: TBase(_starting, _ending)
			{
			}

			UtfStringRangeToStringRangeReader(const TFrom* _starting, vint count)
				: TBase(_starting, count)
			{
			}
		};
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
#if defined VCZH_WCHAR_UTF32
			UtfStreamToStreamReader<char16_t, wchar_t>		reader;
#endif

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
Utf-8
***********************************************************************/
		
		/// <summary>Encoder to write UTF-8 text.</summary>
		class Utf8Encoder : public CharEncoder
		{
		protected:
			vint							WriteString(wchar_t* _buffer, vint chars, bool freeToUpdate);
		};
		
		/// <summary>Decoder to read UTF-8 text.</summary>
		class Utf8Decoder : public CharDecoder
		{
		protected:
			UtfStreamToStreamReader<char8_t, wchar_t>		reader;

			vint							ReadString(wchar_t* _buffer, vint chars);
		public:
		};

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
