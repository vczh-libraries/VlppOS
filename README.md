# VlppOS

**Minimum Operator System Construction.**

## License

This project is licensed under [the License repo](https://github.com/vczh-libraries/License).

This project is not open sourced and for development only. But you are able to use the open sourced version in [the Release repo](https://github.com/vczh-libraries/Release).

## Document

Unfortunately I don't have time to document the code at this moment.
I promise I will do it before GacUI 1.0 get released.
Please learn by unit test at this moment.
Also note that there are comments before important classes.

## Unit Test

- **Locale and Locale-awared String Operation**
  - **TEST** [TestLocale.cpp](./Test/Source/TestLocale.cpp)
  - **TEST** [TestLocalString.cpp](./Test/Source/TestLocaleString.cpp)
- **File System** (Windows only)
  - **TEST** [TestFileSystem.cpp](./Test/Source/TestFileSystem.cpp)
- **Streaming** (FileStream, MemoryStream, compression, encoding, etc)
  - **TEST** [TestStream.cpp](./Test/Source/TestStream.cpp)
- **Multi-Threading**
  - **TEST** [TestThread.cpp](./Test/Source/TestThread.cpp)
