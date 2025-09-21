# Update Knowledge Base

- Only checkout the LATEST chat message, ignore all chat history. It has the request about what to do about the knowledge base.
- Your goal is to update the knowledge base according to the request.

## Implement the Knowledge Base

- Find out the `Accessing Knowledge Base` section. Understand the organization of the knowledge base.
- Read the `Index.md` of the knowledge base first.
- There could be multiple places as the request might include multiple objectives. For each objective:
  - Find out which project does it belong to.
  - Read through all categories, find out which category is the best fit.
    - If there is no obvious best answer, create a new category.A new category comes with a new guideline file, you must add it to the `KnowledgeBase` project.
    - Please be careful about the place to insert the new category, a new category should be added at the end of the belonging project.
  - Update the category description if you think there is anything worth menthioning.
  - Read through the file of the hyperlink in the category, update the content to reflect the change I want you to make.

## Adding or Updating an `API Explanation` guideline

- Content in such guideline must be compact. Do not repeat anything that could be read from the source code.
- Do not simply add code samples.
  - If you do, keep the code sample stick to only usage of APIs.
  - Do not add code sample just to show best practices or what it can do or what it is commonly used or something like that, describe these topics in words.
  - A code sample is only necessary when many functions or classes must be involved in a specific order. A good example would be the `TEST_FILE` structure.

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

