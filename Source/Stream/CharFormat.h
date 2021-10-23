/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_CHARFORMAT
#define VCZH_STREAM_CHARFORMAT

#include "Interfaces.h"

namespace vl
{
	namespace stream
	{

/***********************************************************************
WCharToUtfReader
***********************************************************************/

		template<typename TTo>
		class WCharToUtfReader : public encoding::UtfFrom32ReaderBase<TTo, WCharToUtfReader<TTo>>
		{
			template<typename T, typename TBase>
			friend class encoding::UtfFrom32ReaderBase;

			class InternalReader : public encoding::UtfTo32ReaderBase<wchar_t, InternalReader>
			{
			public:
				const wchar_t* starting = nullptr;
				const wchar_t* ending = nullptr;
				const wchar_t* consuming = nullptr;

				wchar_t Consume()
				{
					if (consuming == ending) return 0;
					return *consuming++;
				}
			};
		protected:
			InternalReader internalReader;

			char32_t Consume()
			{
				return internalReader.Read();
			}
		public:
			WCharToUtfReader(const wchar_t* _starting, vint count)
			{
				internalReader.starting = _starting;
				internalReader.ending = _starting + count;
				internalReader.consuming = _starting;
			}

			bool HasIllegalChar() const
			{
				return encoding::UtfFrom32ReaderBase<TTo, WCharToUtfReader<TTo>>::HasIllegalChar() || internalReader.HasIllegalChar();
			}
		};

/***********************************************************************
StreamToWCharReader
***********************************************************************/

		template<typename TFrom>
		class StreamToWCharReader : public encoding::UtfFrom32ReaderBase<wchar_t, StreamToWCharReader<TFrom>>
		{
			template<typename T, typename TBase>
			friend class encoding::UtfFrom32ReaderBase;

			class InternalReader : public encoding::UtfTo32ReaderBase<TFrom, InternalReader>
			{
			public:
				IStream* stream = nullptr;

				TFrom Consume()
				{
					TFrom c;
					vint size = stream->Read(&c, sizeof(c));
					if (size != sizeof(c)) return 0;
					return c;
				}
			};
		protected:
			InternalReader internalReader;

			char32_t Consume()
			{
				return internalReader.Read();
			}
		public:

			void Setup(IStream* _stream)
			{
				internalReader.stream = _stream;
			}

			bool HasIllegalChar() const
			{
				return encoding::UtfFrom32ReaderBase<wchar_t, StreamToWCharReader<TFrom>>::HasIllegalChar() || internalReader.HasIllegalChar();
			}
		};

/***********************************************************************
Utf16BEStreamToWCharReader
***********************************************************************/

		class Utf16BEStreamToWCharReader : public encoding::UtfFrom32ReaderBase<wchar_t, Utf16BEStreamToWCharReader>
		{
			template<typename T, typename TBase>
			friend class encoding::UtfFrom32ReaderBase;

			class InternalReader : public encoding::UtfTo32ReaderBase<char16_t, InternalReader>
			{
			public:
				IStream* stream = nullptr;

				char16_t Consume()
				{
					char16_t c;
					vint size = stream->Read(&c, sizeof(c));
					if (size != sizeof(c)) return 0;
					vuint8_t* bs = (vuint8_t*)&c;
					vuint8_t t = bs[0];
					bs[0] = bs[1];
					bs[1] = t;
					return c;
				}
			};
		protected:
			InternalReader internalReader;

			char32_t Consume()
			{
				return internalReader.Read();
			}
		public:

			void Setup(IStream* _stream)
			{
				internalReader.stream = _stream;
			}

			bool HasIllegalChar() const
			{
				return encoding::UtfFrom32ReaderBase<wchar_t, Utf16BEStreamToWCharReader>::HasIllegalChar() || internalReader.HasIllegalChar();
			}
		};

/***********************************************************************
Char Encoder and Decoder
***********************************************************************/

		/// <summary>Base type of all character encoder.</summary>
		class CharEncoder : public Object, public IEncoder
		{
		protected:
			IStream*						stream = nullptr;
			vuint8_t						cacheBuffer[sizeof(wchar_t)];
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
			StreamToWCharReader<char16_t>	reader;
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
			Utf16BEStreamToWCharReader		reader;

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
			StreamToWCharReader<char8_t>	reader;

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
