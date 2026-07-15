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

./Bin/UnitTest:./Obj/Vlpp.o ./Obj/Vlpp.Linux.o ./Obj/Base64Encoding.o ./Obj/MbcsEncoding.o ./Obj/UtfEncoding.o ./Obj/Encoding.o ./Obj/LzwEncoding.o ./Obj/FileSystem.o ./Obj/FileSystem.Injectable.o ./Obj/FileSystem.Linux.o ./Obj/AsyncSocket.Linux.o ./Obj/AsyncSocket.macOS.o ./Obj/ChannelPackage.o ./Obj/Locale.o ./Obj/Locale.Linux.o ./Obj/Accessor.o ./Obj/BroadcastStream.o ./Obj/CacheStream.o ./Obj/CharFormat.o ./Obj/CharFormat.Linux.o ./Obj/BomEncoding.o ./Obj/EncodingStream.o ./Obj/FileStream.o ./Obj/MemoryStream.o ./Obj/MemoryWrapperStream.o ./Obj/RecorderStream.o ./Obj/Threading.o ./Obj/Threading.Linux.o ./Obj/TestInterProcess.o ./Obj/TestInterProcess_AsyncSocket.o ./Obj/TestStreamBase64.o ./Obj/TestFileSystem.o ./Obj/TestLocaleString.o ./Obj/TestSerialization.o ./Obj/TestStream.o ./Obj/TestStreamEncoding.o ./Obj/TestStreamLzw.o ./Obj/TestStreamReaderWriter.o ./Obj/TestThread.o ./Obj/Main.o
	$(CPP_LINK)

./Obj/Vlpp.o: ../../Import/Vlpp.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Vlpp.Linux.o: ../../Import/Vlpp.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Base64Encoding.o: ../../Source/Encoding/Base64Encoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/MbcsEncoding.o: ../../Source/Encoding/CharFormat/MbcsEncoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/UtfEncoding.o: ../../Source/Encoding/CharFormat/UtfEncoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Encoding.o: ../../Source/Encoding/Encoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/LzwEncoding.o: ../../Source/Encoding/LzwEncoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/FileSystem.o: ../../Source/FileSystem.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/FileSystem.Injectable.o: ../../Source/FileSystem.Injectable.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/FileSystem.Linux.o: ../../Source/FileSystem.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket.Linux.o: ../../Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket.macOS.o: ../../Source/InterProcess/AsyncSocket/AsyncSocket.macOS.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/ChannelPackage.o: ../../Source/InterProcess/ChannelImpls/ChannelPackage.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Locale.o: ../../Source/Locale.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Locale.Linux.o: ../../Source/Locale.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Accessor.o: ../../Source/Stream/Accessor.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/BroadcastStream.o: ../../Source/Stream/BroadcastStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/CacheStream.o: ../../Source/Stream/CacheStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/CharFormat.o: ../../Source/Encoding/CharFormat/CharFormat.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/CharFormat.Linux.o: ../../Source/Encoding/CharFormat/CharFormat.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/BomEncoding.o: ../../Source/Encoding/CharFormat/BomEncoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/EncodingStream.o: ../../Source/Stream/EncodingStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/FileStream.o: ../../Source/Stream/FileStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/MemoryStream.o: ../../Source/Stream/MemoryStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/MemoryWrapperStream.o: ../../Source/Stream/MemoryWrapperStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/RecorderStream.o: ../../Source/Stream/RecorderStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Threading.o: ../../Source/Threading.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Threading.Linux.o: ../../Source/Threading.Linux.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestInterProcess.o: ../Source/TestInterProcess.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestInterProcess_AsyncSocket.o: ../Source/TestInterProcess_AsyncSocket.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestStreamBase64.o: ../Source/TestStreamBase64.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestFileSystem.o: ../Source/TestFileSystem.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestLocaleString.o: ../Source/TestLocaleString.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestSerialization.o: ../Source/TestSerialization.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestStream.o: ../Source/TestStream.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestStreamEncoding.o: ../Source/TestStreamEncoding.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestStreamLzw.o: ../Source/TestStreamLzw.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestStreamReaderWriter.o: ../Source/TestStreamReaderWriter.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/TestThread.o: ../Source/TestThread.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)

./Obj/Main.o: Main.cpp $(VCPROOT)/vl/makefile-cpp
	$(CPP_COMPILE)
