# Task

- Find out the `Accessing Knowledge Base` section. Understand the organization of the knowledge base.
- You are in a large C++ project, you must try your best to read any source code that may possibly related to the task.
- Follow the chat message to implement the task.
- After any code change, find the `Verifying your code edit` section, it has everything you need to know about how to verify your code edit.

# General Instruction

- You are on Windows running in Visual Studio Code.
- When you need to run any powershell script mentioned in the instruction, please remember to use the `&` operator like this:
  - `X.ps1`: invalid command.
  - `..\X.ps1`: valid command but it doesn't work with you.
  - `& X.ps1` or `& ..\X.ps1`: good.
  
- Before saying anything, say "Yes, vczh!". I use it to make sure instruction files are taking effect.
- Find out the `Accessing Knowledge Base` section, read `Index.md` of `KnowledgeBase` project in the current solution.
- Before generating any code, if the file is changed, read it. Not all changes come from you, I will edit the file too. Do not generate code based on out-dated version in your memory.
- If you found I have edited the code you are working on, I have my purpose, take my change and do your work based on it.
- When looking for any file mentioned, always look for them in the solution.
  - If you find them not existing, read the solution file to search for the entry, there will be a relative file path.
- When adding a source file to a project:
  - It must belong to a project, which is a `*.vcxproj` or `*.vcxitems` file.
  - It is an XML file.
  - Edit that project file to include the source file.
- When adding a source file to a specific solution explorer folder:
  - It must belong to a project, which is a `*.vcxproj` or `*.vcxitems` file.
  - Find the `*.filters` file with the same name, it is an XML file.
  - Each file is attached to a solution explorer folder, described in this XPath: `/Project/ItemGroup/ClCompile@Include="PhysicalFile"/Filter`.
  - In side the `Filter` tag there is the solution explorer folder.
  - Edit that `*.filters` file to include the source file.

# Compile the Solution

- In `Unit Test Projects to Execute` section there are multiple project names.
- These projects are all `*.vcxproj` files. Locate them. In the parent folder there must be a `*.sln` file. That is the solution the compile.
- You must move the current working directory to the folder containing the `*.sln` file.
  - The `ls` command helps.
  - This must be done because `copilotBuild.ps1` searches `*.sln` from the working directory, otherwise it will fail.
- Execute `copilotBuild.ps1`.
- DO NOT use msbuild by yourself.
- You must keep fixing the code until all errors are eliminated.

# Verifying your code edit

- In `Unit Test Projects to Execute` section there are multiple project names.
- These projects are all `*.vcxproj` files. Locate them. In the parent folder there must be a `*.sln` file. That is the solution the compile.
- You must move the current working directory to the folder containing the `*.sln` file.
  - The `ls` command helps.
  - This must be done because `copilotExecute.ps1` searches `*.sln` from the working directory, otherwise it will fail.
- You must verify your code by executing each project in order. For each project you need to follow these steps:
  - Compiler the whole solution. Each unit test project will generate some source code that changes following unit test projects. That's why you need to compile before each execution.
  - Execute `copilotExecute.ps1 -Executable <PROJECT-NAME>`. `<PROJECT-NAME>` is the project name in the list.
- You must keep fixing the code until all errors are eliminated.

## Unit Test Projects to Execute

- `UnitTest`

### Calling copilotBuild.ps1 and copilotExecute.ps1

This solution is in `Test\UnitTest`, after `ls` to this folder, scripts will be accessible with:
- `& ..\..\.github\TaskLogs\copilotBuild.ps1`
- `& ..\..\.github\TaskLogs\copilotExecute.ps1`

