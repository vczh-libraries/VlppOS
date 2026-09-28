.PHONY: all clean pre-build
.DEFAULT_GOAL := all

CPP_TARGET=./Bin/MiniHttpServer
CPP_OBJECTS=./Obj/Vlpp.o ./Obj/Vlpp.Linux.o ./Obj/MbcsEncoding.o ./Obj/UtfEncoding.o ./Obj/Encoding.o ./Obj/FileSystem.o ./Obj/FileSystem.Injectable.o ./Obj/FileSystem.Linux.o ./Obj/AsyncSocket.o ./Obj/AsyncSocket_HttpRequest.o ./Obj/AsyncSocket_HttpRequestServer.o ./Obj/AsyncSocket_HttpServerApi.o ./Obj/NetworkProtocolHttp.o ./Obj/AsyncSocket.Linux.o ./Obj/AsyncSocket.macOS.o ./Obj/Locale.o ./Obj/Locale.Linux.o ./Obj/Accessor.o ./Obj/CharFormat.o ./Obj/CharFormat.Linux.o ./Obj/BomEncoding.o ./Obj/EncodingStream.o ./Obj/FileStream.o ./Obj/MemoryStream.o ./Obj/MemoryWrapperStream.o ./Obj/Threading.o ./Obj/Threading.Linux.o ./Obj/Main.o

CPP_COMPILE_OPTIONS=-I ../../../Import
include $(VCPROOT)/vl/makefile-cpp

pre-build:
	if ! [ -d ./Bin ]; then mkdir ./Bin; fi
	if ! [ -d ./Obj ]; then mkdir ./Obj; fi
	if ! [ -d ./Coverage ]; then mkdir ./Coverage; fi

clean:
	rm -f "$(CPP_TARGET)" "$(dir $(CPP_TARGET))app.mjs" "$(dir $(CPP_TARGET))app.wasm" "$(dir $(CPP_TARGET))app.worker.js" "$(dir $(CPP_TARGET))app.html" "$(dir $(CPP_TARGET))app.sh" "$(dir $(CPP_TARGET))index.html"
	if [ -d ./Bin ]; then rm -r ./Bin; fi
	if [ -d ./Obj ]; then rm -r ./Obj; fi
	if [ -d ./Coverage ]; then rm -r ./Coverage; fi

all:pre-build ./Bin/MiniHttpServer

$(CPP_TARGET) $(CPP_OBJECTS): | pre-build

$(CPP_TARGET):$(CPP_OBJECTS)
	$(CPP_LINK)

./Obj/Vlpp.o: ../../../Import/Vlpp.cpp
	$(CPP_COMPILE)

./Obj/Vlpp.Linux.o: ../../../Import/Vlpp.Linux.cpp
	$(CPP_COMPILE)

./Obj/MbcsEncoding.o: ../../../Source/Encoding/CharFormat/MbcsEncoding.cpp
	$(CPP_COMPILE)

./Obj/UtfEncoding.o: ../../../Source/Encoding/CharFormat/UtfEncoding.cpp
	$(CPP_COMPILE)

./Obj/Encoding.o: ../../../Source/Encoding/Encoding.cpp
	$(CPP_COMPILE)

./Obj/FileSystem.o: ../../../Source/FileSystem.cpp
	$(CPP_COMPILE)

./Obj/FileSystem.Injectable.o: ../../../Source/FileSystem.Injectable.cpp
	$(CPP_COMPILE)

./Obj/FileSystem.Linux.o: ../../../Source/FileSystem.Linux.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket_HttpRequest.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequest.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket_HttpRequestServer.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpRequestServer.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket_HttpServerApi.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket_HttpServerApi.cpp
	$(CPP_COMPILE)

./Obj/NetworkProtocolHttp.o: ../../../Source/InterProcess/NetworkProtocolHttp.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket.Linux.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket.Linux.cpp
	$(CPP_COMPILE)

./Obj/AsyncSocket.macOS.o: ../../../Source/InterProcess/AsyncSocket/AsyncSocket.macOS.cpp
	$(CPP_COMPILE)

./Obj/Locale.o: ../../../Source/Locale.cpp
	$(CPP_COMPILE)

./Obj/Locale.Linux.o: ../../../Source/Locale.Linux.cpp
	$(CPP_COMPILE)

./Obj/Accessor.o: ../../../Source/Stream/Accessor.cpp
	$(CPP_COMPILE)

./Obj/CharFormat.o: ../../../Source/Encoding/CharFormat/CharFormat.cpp
	$(CPP_COMPILE)

./Obj/CharFormat.Linux.o: ../../../Source/Encoding/CharFormat/CharFormat.Linux.cpp
	$(CPP_COMPILE)

./Obj/BomEncoding.o: ../../../Source/Encoding/CharFormat/BomEncoding.cpp
	$(CPP_COMPILE)

./Obj/EncodingStream.o: ../../../Source/Stream/EncodingStream.cpp
	$(CPP_COMPILE)

./Obj/FileStream.o: ../../../Source/Stream/FileStream.cpp
	$(CPP_COMPILE)

./Obj/MemoryStream.o: ../../../Source/Stream/MemoryStream.cpp
	$(CPP_COMPILE)

./Obj/MemoryWrapperStream.o: ../../../Source/Stream/MemoryWrapperStream.cpp
	$(CPP_COMPILE)

./Obj/Threading.o: ../../../Source/Threading.cpp
	$(CPP_COMPILE)

./Obj/Threading.Linux.o: ../../../Source/Threading.Linux.cpp
	$(CPP_COMPILE)

./Obj/Main.o: ../../UnitTest/MiniHttpServer/Main.cpp
	$(CPP_COMPILE)
