The last work in .github/TaskLogs/Copilot_Investigate.md has just been done, the work must be also verified in downstream repos:
- StartRpcStdio.ps1 in Workflow
- StartRpcStdio.ps1 in GacJS
- GacUI/.github/Jobs/job.xp(Windows|Plat).prompt.md
  - Since we only need to verify if the network protocol upgrade is successful or not, you can limit the verification on `rvmt` app, but try all combinations.
  - This will save some time.

Before doing verification, you need to release VlppOS to Workflow. When Workflow is verified, release VlppOS and Workflow to GacUI, continue the rest of the verification.

You might also need to verify if any knowledge base page is affected by the original work, you can perform the change in Tools repo.
GacJS documents and website might also needs to check. If the website is changed, publish the website immediately.
commit and push all local changes once finishing.
