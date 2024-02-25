/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_STREAM_INTERFACES
#define VCZH_STREAM_INTERFACES

#include <Vlpp.h>

namespace vl
{
	namespace stream
	{
		/// <summary>
		/// <p>
		/// Interface for streams.
		/// </p>
		/// <p>
		/// Please notice that, even if you get a stream object, if [M:vl.stream.IStream.IsAvailable] returns false, all other methods cannot be used.
		/// </p>
		/// <p>
		/// Not all methods are available for all types of streams.
		/// Feature testing functions must be called before calling other methods, if it is not sure that what kind of stream is being operated against:
		/// </p>
		/// <p>
		///   <ul>
		///     <li><b>Readable</b>: A stream is readable if [M:vl.stream.IStream.CanRead] returns true.</li>
		///     <li><b>Peekable</b>: A stream is peekable if [M:vl.stream.IStream.CanPeek] returns true.</li>
		///     <li><b>Writable</b>: A stream is writable if [M:vl.stream.IStream.CanWrite] returns true.</li>
		///     <li><b>Seekable</b>: A stream is readable if [M:vl.stream.IStream.CanSeek] returns true.</li>
		///     <li><b>Finite</b>: A stream is finite if [M:vl.stream.IStream.IsLimited] returns true.</li>
		///   </ul>
		/// </p>
		/// </summary>
		class IStream : public virtual Interface
		{
		public:
			/// <summary>Test if the stream is <b>readable</b>.</summary>
			/// <returns>Returns true if the stream is <b>readable</b>.</returns>
			virtual bool					CanRead()const=0;
			/// <summary>Test if the stream is <b>writable</b>.</summary>
			/// <returns>Returns true if the stream is <b>writable</b>.</returns>
			virtual bool					CanWrite()const=0;
			/// <summary>Test if the stream is <b>seekable</b>.</summary>
			/// <returns>Returns true if the stream is <b>seekable</b>.</returns>
			virtual bool					CanSeek()const=0;
			/// <summary>Test if the stream is <b>peekable</b>.</summary>
			/// <returns>Returns true if the stream is <b>peekable</b>.</returns>
			virtual bool					CanPeek()const=0;
			/// <summary>Test if the content of the stream is <b>finite</b>. A writable stream can also be limited, it means that you can only write limited content to the stream.</summary>
			/// <returns>Returns true if the content of the stream is <b>finite</b>.</returns>
			virtual bool					IsLimited()const=0;
			/// <summary>Test if the stream is <b>available</b>. For example, if you create a readable [T:vl.stream.FileStream] giving a wrong file name, it will be unavailable.</summary>
			/// <returns>Returns true if the stream is <b>available</b>.</returns>
			virtual bool					IsAvailable()const=0;
			/// <summary>Close the stream, making the stream <b>unavailable</b>.</summary>
			virtual void					Close()=0;
			/// <summary>Get the current position in the stream.</summary>
			/// <returns>The position in the stream. Returns -1 if the stream is <b>unavailable</b>.</returns>
			virtual pos_t					Position()const=0;
			/// <summary>Get the size of the content in this stream.</summary>
			/// <returns>The size of the content in this stream. Returns -1 if the size is <b>unsizable</b> or <b>unavailable</b>.</returns>
			virtual pos_t					Size()const=0;
			/// <summary>Step forward or backward from the current position. It will crash if the stream is <b>unseekable</b> or <b>unavailable</b>.</summary>
			/// <param name="_size">The length to step forward if it is a positive number. The length to step backward if it is a negative number</param>
			virtual void					Seek(pos_t _size)=0;
			/// <summary>Step forward from the beginning. It will crash if the stream is <b>unseekable</b> or <b>unavailable</b>.</summary>
			/// <param name="_size">The length to step forward.</param>
			virtual void					SeekFromBegin(pos_t _size)=0;
			/// <summary>Step backward from the end. It will crash if the stream is <b>unseekable</b> or <b>unavailable</b>.</summary>
			/// <param name="_size">The length to step backward.</param>
			virtual void					SeekFromEnd(pos_t _size)=0;
			/// <summary>Read from the current position and step forward. It will crash if the stream is <b>unreadable</b> or <b>unavailable</b>.</summary>
			/// <returns>Returns the actual size of the content that has read. Returns 0 if a stream has no more data to read.</returns>
			/// <param name="_buffer">A buffer to store the content.</param>
			/// <param name="_size">The size of the content that is expected to read.</param>
			virtual vint					Read(void* _buffer, vint _size)=0;
			/// <summary>Write to the current position and step forward. It will crash if the stream is <b>unwritable</b> or <b>unavailable</b>.</summary>
			/// <returns>Returns the actual size of the content that has written. Returns 0 if a stream has not enough space to write.</returns>
			/// <param name="_buffer">A buffer storing the content to write.</param>
			/// <param name="_size">The size of the content that is expected to write.</param>
			virtual vint					Write(void* _buffer, vint _size)=0;
			/// <summary>Read from the current position without stepping forward. It will crash if the stream is <b>unpeekable</b> or <b>unavailable</b>.</summary>
			/// <returns>Returns the actual size of the content that is read. Returns 0 if a stream has no more data to read.</returns>
			/// <param name="_buffer">A buffer to store the content.</param>
			/// <param name="_size">The size of the content that is expected to read.</param>
			virtual vint					Peek(void* _buffer, vint _size)=0;
		};
	}
}

#endif