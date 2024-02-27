.PHONY: all clean pre-build
.DEFAULT_GOAL := all

CPP_COMPILE_OPTIONS=-I ../../Import
include $(VCPROOT)/vl/makefile-cpp

pre-build:
	if ! [ -d ./Bin ]; then mkdir ./Bin; fi
	if ! [ -d ./Obj ]; then mkdir ./Obj; fi
	if ! [ -d ./Coverage ]; then mkdir ./Coverage; fi
	if ! [ -d ../Output ]; then mkdir ../Output; fi

clean:
	if [ -d ./Bin ]; then rm -r ./Bin; fi
	if [ -d ./Obj ]; then rm -r ./Obj; fi
	if [ -d ./Coverage ]; then rm -r ./Coverage; fi
	if [ -d ./../Output ]; then rm -r ../Output; fi

all:pre-build ./Bin/UnitTest

./Bin/UnitTest:./Obj/Vlpp.o ./Obj/Vlpp.Linux.o ./Obj/Base64Encoding.o ./Obj/MbcsEncoding.o ./Obj/UtfEncoding.o ./Obj/Encoding.o ./Obj/LzwEncoding.o ./Obj/FileSystem.o ./Obj/FileSystem.Linux.o ./Obj/Locale.o ./Obj/Locale.Linux.o ./Obj/Accessor.o ./Obj/BroadcastStream.o ./Obj/CacheStream.o ./Obj/CharFormat.o ./Obj/CharFormat.Linux.o ./Obj/BomEncoding.o ./Obj/EncodingStream.o ./Obj/FileStream.o ./Obj/MemoryStream.o ./Obj/MemoryWrapperStream.o ./Obj/RecorderStream.o ./Obj/Threading.o ./Obj/Threading.Linux.o ./Obj/TestStreamBase64.o ./Obj/TestFileSystem.o ./Obj/TestLocaleString.o ./Obj/TestSerialization.o ./Obj/TestStream.o ./Obj/TestStreamEncoding.o ./Obj/TestStreamLzw.o ./Obj/TestStreamReaderWriter.o ./Obj/TestThread.o ./Obj/Main.o
	$(CPP_LINK)

./Obj/Vlpp.o: ../../Import/Vlpp.cpp ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Vlpp.Linux.o: ../../Import/Vlpp.Linux.cpp ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Base64Encoding.o: ../../Source/Encoding/Base64Encoding.cpp ../../Source/Encoding/Base64Encoding.h ../../Source/Encoding/Encoding.h ../../Source/Encoding/../Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/MbcsEncoding.o: ../../Source/Encoding/CharFormat/MbcsEncoding.cpp ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/BomEncoding.h
	$(CPP_COMPILE)

./Obj/UtfEncoding.o: ../../Source/Encoding/CharFormat/UtfEncoding.cpp ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Encoding.o: ../../Source/Encoding/Encoding.cpp ../../Source/Encoding/Encoding.h ../../Source/Encoding/../Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/LzwEncoding.o: ../../Source/Encoding/LzwEncoding.cpp ../../Source/Encoding/LzwEncoding.h ../../Source/Encoding/Encoding.h ../../Source/Encoding/../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/../Stream/Accessor.h ../../Source/Encoding/../Stream/MemoryStream.h ../../Source/Encoding/../Stream/EncodingStream.h ../../Source/Encoding/../Stream/../Encoding/Encoding.h ../../Source/Encoding/../Stream/MemoryWrapperStream.h
	$(CPP_COMPILE)

./Obj/FileSystem.o: ../../Source/FileSystem.cpp ../../Source/FileSystem.h ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/BomEncoding.h ../../Source/Locale.h ../../Source/Stream/FileStream.h ../../Source/Stream/MemoryWrapperStream.h ../../Source/Stream/Accessor.h ../../Source/Encoding/CharFormat/../../Stream/MemoryStream.h ../../Source/Stream/EncodingStream.h ../../Source/Encoding/CharFormat/../../Stream/../Encoding/Encoding.h
	$(CPP_COMPILE)

./Obj/FileSystem.Linux.o: ../../Source/FileSystem.Linux.cpp ../../Source/FileSystem.h ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/BomEncoding.h ../../Source/Locale.h ../../Source/Stream/FileStream.h ../../Source/Stream/MemoryWrapperStream.h ../../Source/Stream/Accessor.h ../../Source/Encoding/CharFormat/../../Stream/MemoryStream.h ../../Source/Stream/EncodingStream.h ../../Source/Encoding/CharFormat/../../Stream/../Encoding/Encoding.h
	$(CPP_COMPILE)

./Obj/Locale.o: ../../Source/Locale.cpp ../../Source/Locale.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Locale.Linux.o: ../../Source/Locale.Linux.cpp ../../Source/Locale.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Accessor.o: ../../Source/Stream/Accessor.cpp ../../Source/Stream/Accessor.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Stream/MemoryStream.h
	$(CPP_COMPILE)

./Obj/BroadcastStream.o: ../../Source/Stream/BroadcastStream.cpp ../../Source/Stream/BroadcastStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/CacheStream.o: ../../Source/Stream/CacheStream.cpp ../../Source/Stream/CacheStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/CharFormat.o: ../../Source/Encoding/CharFormat/CharFormat.cpp ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/BomEncoding.h
	$(CPP_COMPILE)

./Obj/CharFormat.Linux.o: ../../Source/Encoding/CharFormat/CharFormat.Linux.cpp ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/UtfEncoding.h ../../Source/Encoding/CharFormat/BomEncoding.h
	$(CPP_COMPILE)

./Obj/BomEncoding.o: ../../Source/Encoding/CharFormat/BomEncoding.cpp ../../Source/Encoding/CharFormat/BomEncoding.h ../../Source/Encoding/CharFormat/../Encoding.h ../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../../Source/Encoding/CharFormat/CharFormat.h ../../Source/Encoding/CharFormat/MbcsEncoding.h ../../Source/Encoding/CharFormat/UtfEncoding.h
	$(CPP_COMPILE)

./Obj/EncodingStream.o: ../../Source/Stream/EncodingStream.cpp ../../Source/Stream/EncodingStream.h ../../Source/Stream/../Encoding/Encoding.h ../../Source/Stream/../Encoding/../Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/FileStream.o: ../../Source/Stream/FileStream.cpp ../../Source/Stream/FileStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/MemoryStream.o: ../../Source/Stream/MemoryStream.cpp ../../Source/Stream/MemoryStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/MemoryWrapperStream.o: ../../Source/Stream/MemoryWrapperStream.cpp ../../Source/Stream/MemoryWrapperStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/RecorderStream.o: ../../Source/Stream/RecorderStream.cpp ../../Source/Stream/RecorderStream.h ../../Source/Stream/Interfaces.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Threading.o: ../../Source/Threading.cpp ../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Threading.Linux.o: ../../Source/Threading.Linux.cpp ../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/TestStreamBase64.o: ../Source/TestStreamBase64.cpp ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/EncodingStream.h ../Source/../../Source/Stream/../Encoding/Encoding.h ../Source/../../Source/Stream/../Encoding/../Stream/Interfaces.h ../Source/../../Source/Encoding/Base64Encoding.h
	$(CPP_COMPILE)

./Obj/TestFileSystem.o: ../Source/TestFileSystem.cpp ../Source/../../Source/FileSystem.h ../Source/../../Source/Encoding/CharFormat/CharFormat.h ../Source/../../Source/Encoding/CharFormat/MbcsEncoding.h ../Source/../../Source/Encoding/CharFormat/../Encoding.h ../Source/../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Encoding/CharFormat/UtfEncoding.h ../Source/../../Source/Encoding/CharFormat/BomEncoding.h ../Source/../../Source/Locale.h ../Source/../../Source/Stream/FileStream.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Encoding/CharFormat/../../Stream/MemoryStream.h ../Source/../../Source/Stream/EncodingStream.h ../Source/../../Source/Encoding/CharFormat/../../Stream/../Encoding/Encoding.h ../Source/../../Source/Threading.h
	$(CPP_COMPILE)

./Obj/TestLocaleString.o: ../Source/TestLocaleString.cpp ../Source/../../Source/Locale.h ../../Import/Vlpp.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/Interfaces.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Encoding/CharFormat/CharFormat.h ../Source/../../Source/Encoding/CharFormat/MbcsEncoding.h ../Source/../../Source/Encoding/CharFormat/../Encoding.h ../Source/../../Source/Encoding/CharFormat/../../Stream/Interfaces.h ../Source/../../Source/Encoding/CharFormat/UtfEncoding.h ../Source/../../Source/Encoding/CharFormat/BomEncoding.h ../Source/../../Source/Stream/FileStream.h
	$(CPP_COMPILE)

./Obj/TestSerialization.o: ../Source/TestSerialization.cpp ../Source/../../Source/Stream/Serialization.h ../Source/../../Source/Stream/../Encoding/CharFormat/CharFormat.h ../Source/../../Source/Stream/../Encoding/CharFormat/MbcsEncoding.h ../Source/../../Source/Stream/../Encoding/CharFormat/../Encoding.h ../Source/../../Source/Stream/../Encoding/CharFormat/../../Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/../Encoding/CharFormat/UtfEncoding.h ../Source/../../Source/Stream/../Encoding/CharFormat/BomEncoding.h ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Stream/Interfaces.h ../Source/../../Source/Stream/MemoryStream.h
	$(CPP_COMPILE)

./Obj/TestStream.o: ../Source/TestStream.cpp ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/FileStream.h ../Source/../../Source/Stream/RecorderStream.h ../Source/../../Source/Stream/BroadcastStream.h ../Source/../../Source/Stream/CacheStream.h
	$(CPP_COMPILE)

./Obj/TestStreamEncoding.o: ../Source/TestStreamEncoding.cpp ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/EncodingStream.h ../Source/../../Source/Stream/../Encoding/Encoding.h ../Source/../../Source/Stream/../Encoding/../Stream/Interfaces.h ../Source/../../Source/Encoding/CharFormat/CharFormat.h ../Source/../../Source/Encoding/CharFormat/MbcsEncoding.h ../Source/../../Source/Encoding/CharFormat/../Encoding.h ../Source/../../Source/Encoding/CharFormat/UtfEncoding.h ../Source/../../Source/Encoding/CharFormat/BomEncoding.h ../Source/../../Source/Locale.h
	$(CPP_COMPILE)

./Obj/TestStreamLzw.o: ../Source/TestStreamLzw.cpp ../Source/../../Source/Stream/FileStream.h ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/MemoryStream.h ../Source/../../Source/Stream/EncodingStream.h ../Source/../../Source/Stream/../Encoding/Encoding.h ../Source/../../Source/Stream/../Encoding/../Stream/Interfaces.h ../Source/../../Source/Encoding/LzwEncoding.h
	$(CPP_COMPILE)

./Obj/TestStreamReaderWriter.o: ../Source/TestStreamReaderWriter.cpp ../Source/../../Source/Stream/MemoryWrapperStream.h ../Source/../../Source/Stream/Interfaces.h ../../Import/Vlpp.h ../Source/../../Source/Stream/Accessor.h ../Source/../../Source/Stream/MemoryStream.h
	$(CPP_COMPILE)

./Obj/TestThread.o: ../Source/TestThread.cpp ../Source/../../Source/Threading.h ../../Import/Vlpp.h
	$(CPP_COMPILE)

./Obj/Main.o: Main.cpp ../../Import/Vlpp.h
	$(CPP_COMPILE)
