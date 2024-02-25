/***********************************************************************
Author: Zihan Chen (vczh)
Licensed under https://github.com/vczh-libraries/License
***********************************************************************/

#ifndef VCZH_FILESYSTEM
#define VCZH_FILESYSTEM

#include "Encoding/CharFormat/CharFormat.h"

namespace vl
{
	namespace filesystem
	{
		/// <summary>Absolute file path.</summary>
		class FilePath : public Object
		{
		protected:
			WString						fullPath;

			static void					NormalizeDelimiters(collections::Array<wchar_t>& buffer);
			static void					TrimLastDelimiter(WString& fullPath);
			void						Initialize();

			static void					GetPathComponents(WString path, collections::List<WString>& components);
			static WString				ComponentsToPath(const collections::List<WString>& components);
		public:
#if defined VCZH_MSVC
			/// <summary>The delimiter character used in a file path</summary>
			/// <remarks>
			/// In Windows, it is "\".
			/// In Linux and macOS, it is "/".
			/// But you can always use "/", it is also supported in Windows.
			/// </remarks>
			static const wchar_t		Delimiter = L'\\';
#elif defined VCZH_GCC
			static const wchar_t		Delimiter = L'/';
#endif

			/// <summary>Create a root path.</summary>
			/// <remarks><see cref="GetFullPath"/> returns different values for root path on different platforms. Do not rely on the value.</remarks>
			FilePath();
			/// <summary>Create a file path.</summary>
			/// <param name="_filePath">Content of the file path. If it is a relative path, it will be converted to an absolute path.</param>
			FilePath(const WString& _filePath);
			/// <summary>Create a file path.</summary>
			/// <param name="_filePath">Content of the file path. If it is a relative path, it will be converted to an absolute path.</param>
			FilePath(const wchar_t* _filePath);
			/// <summary>Copy a file path.</summary>
			/// <param name="_filePath">The file path to copy.</param>
			FilePath(const FilePath& _filePath);
			~FilePath() = default;

			std::strong_ordering		operator<=>(const FilePath& path)const { return fullPath <=> path.fullPath; }
			bool						operator==(const FilePath& path)const { return fullPath == path.fullPath; }

			/// <summary>Concat an absolute path and a relative path.</summary>
			/// <returns>The result absolute path.</returns>
			/// <param name="relativePath">The relative path to concat.</param>
			FilePath					operator/(const WString& relativePath)const;

			/// <summary>Test if the file path is a file.</summary>
			/// <returns>Returns true if the file path is a file.</returns>
			bool						IsFile()const;
			/// <summary>Test if the file path is a folder.</summary>
			/// <returns>Returns true if the file path is a folder.</returns>
			/// <remarks>In Windows, a drive is also considered a folder.</remarks>
			bool						IsFolder()const;
			/// <summary>Test if the file path is a the root of all file system objects.</summary>
			/// <returns>Returns true if the file path is the root of all file system objects.</returns>
			bool						IsRoot()const;

			/// <summary>Get the last piece of names in the file path.</summary>
			/// <returns>The last piece of names in the file path.</returns>
			WString						GetName()const;
			/// <summary>Get the containing folder of this file path.</summary>
			/// <returns>The containing folder.</returns>
			FilePath					GetFolder()const;
			/// <summary>Get the content of the file path.</summary>
			/// <returns>The content of the file path.</returns>
			WString						GetFullPath()const;
			/// <summary>Calculate the relative path based on a specified referencing folder.</summary>
			/// <returns>The relative path.</returns>
			/// <param name="_filePath">The referencing folder.</param>
			WString						GetRelativePathFor(const FilePath& _filePath)const;

		};

		/// <summary>A file.</summary>
		class File : public Object
		{
		private:
			FilePath					filePath;

		public:
			/// <summary>Create an empty reference. An empty reference does not refer to any file.</summary>
			File() = default;
			/// <summary>Create a reference to a specified file. The file is not required to exist.</summary>
			/// <param name="_filePath">The specified file.</param>
			File(const FilePath& _filePath);
			~File() = default;

			/// <summary>Get the file path of the file.</summary>
			/// <returns>The file path.</returns>
			const FilePath&				GetFilePath()const;

			/// <summary>Get the content of a text file with encoding testing.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="text">Returns the content of the file.</param>
			/// <param name="encoding">Returns the encoding of the file.</param>
			/// <param name="containsBom">Returns true if there is a BOM in the file.</param>
			bool						ReadAllTextWithEncodingTesting(WString& text, stream::BomEncoder::Encoding& encoding, bool& containsBom);
			/// <summary>Get the content of a text file. If there is no BOM in the file, the encoding is assumed to be aligned to the current code page.</summary>
			/// <returns>The content of the file.</returns>
			WString						ReadAllTextByBom()const;
			/// <summary>Get the content of a text file.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="text">The content of the file.</param>
			bool						ReadAllTextByBom(WString& text)const;
			/// <summary>Get the content of a text file by lines.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="lines">The content of the file by lines.</param>
			/// <remarks>
			/// Lines could be separated by either CRLF or LF.
			/// A text file is not required to ends with CRLF.
			/// If the last character of the file is LF,
			/// the last line is the line before LF.
			/// </remarks>
			bool						ReadAllLinesByBom(collections::List<WString>& lines)const;

			/// <summary>Write text to the file.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="text">The text to write.</param>
			/// <param name="bom">Set to true to add a corresponding BOM at the beginning of the file according to the encoding, the default value is true.</param>
			/// <param name="encoding">The text encoding, the default encoding is UTF-16.</param>
			bool						WriteAllText(const WString& text, bool bom = true, stream::BomEncoder::Encoding encoding = stream::BomEncoder::Utf16);
			/// <summary>Write text to the file.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="lines">The text to write, with CRLF appended after all lines.</param>
			/// <param name="bom">Set to true to add a corresponding BOM at the beginning of the file according to the encoding, the default value is true.</param>
			/// <param name="encoding">The text encoding, the default encoding is UTF-16.</param>
			bool						WriteAllLines(collections::List<WString>& lines, bool bom = true, stream::BomEncoder::Encoding encoding = stream::BomEncoder::Utf16);
			
			/// <summary>Test does the file exist or not.</summary>
			/// <returns>Returns true if the file exists.</returns>
			bool						Exists()const;
			/// <summary>Delete the file.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <remarks>This function could return before the file is actually deleted.</remarks>
			bool						Delete()const;
			/// <summary>Rename the file.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="newName">The new file name.</param>
			bool						Rename(const WString& newName)const;
		};
		
		/// <summary>A folder.</summary>
		/// <remarks>In Windows, a drive is also considered a folder.</remarks>
		class Folder : public Object
		{
		private:
			FilePath					filePath;

			bool						CreateNonRecursively()const;
			bool						DeleteNonRecursively()const;
		public:
			/// <summary>Create a reference to the root folder.</summary>
			Folder() = default;
			/// <summary>Create a reference to a specified folder. The folder is not required to exist.</summary>
			/// <param name="_filePath">The specified folder.</param>
			Folder(const FilePath& _filePath);
			~Folder() = default;
			
			/// <summary>Get the file path of the folder.</summary>
			/// <returns>The file path.</returns>
			const FilePath&				GetFilePath()const;
			/// <summary>Get all folders in this folder.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="folders">All folders.</param>
			/// <remarks>In Windows, drives are considered sub folders in the root folder.</remarks>
			bool						GetFolders(collections::List<Folder>& folders)const;
			/// <summary>Get all files in this folder.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="files">All files.</param>
			bool						GetFiles(collections::List<File>& files)const;
			
			/// <summary>Test does the folder exist or not.</summary>
			/// <returns>Returns true if the folder exists.</returns>
			bool						Exists()const;
			/// <summary>Create the folder.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="recursively">Set to true to create all levels of containing folders if they do not exist.</param>
			/// <remarks>
			/// This function could return before the folder is actually created.
			/// If "recursively" is false, this function will only attempt to create the specified folder directly,
			/// it fails if the containing folder does not exist.
			/// </remarks>
			bool						Create(bool recursively)const;
			/// <summary>Delete the folder.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="recursively">Set to true to delete everything in the folder.</param>
			/// <remarks>This function could return before the folder is actually deleted.</remarks>
			bool						Delete(bool recursively)const;
			/// <summary>Rename the folder.</summary>
			/// <returns>Returns true if this operation succeeded.</returns>
			/// <param name="newName">The new folder name.</param>
			bool						Rename(const WString& newName)const;
		};
	}
}

#endif
