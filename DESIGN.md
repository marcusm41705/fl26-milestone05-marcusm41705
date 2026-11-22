# Milestone 5 Decision Record

Milestone 4 Fixes:
There were no specific fixes for the Milestone 4 implementation. Existing functionality for Milestone 4 was a priority during the implementation and was retained while replacing the background build system and lifecycle management for corpus for Milestone 5's implementation. 
Threads and Shared Data:
The GUI, Workspace, current ProcessingCore, and corpus state objects are all owned by MainWindow and remain on the GUI thread. BuildWorker owns one persistent instance of the std::thread, which is used to process corpus builds while using a copy /snapshot of the workspace. The background worker uses std::mutex and std::condition_variable for synchronization purposes. Mutex is used to protect pending_request_, stopping_, gen_, and phase_. The std::condition_variable will allow the worker to wait without having to poll. The worker then sends the completed results to the GUI using QMetaObject::invokeMethod() and Qt:QueuedConnection.

All pieces of mutable data reachable from both threads are atomic or protectted by a mutex. Workplace copies / snapshots and processing results both have separate ownership, which avoids the concurrent modification of data owned by the GUI.

States:
The corpus lifecycle uses five state classes that were derived from CorpusState.
The link to the state table is here:
https://docs.google.com/document/d/1SYoF4mGVeiipaxMHRapRX2SwdbD6E4c_r0DaoxS_78Q/edit?usp=sharing
Operational states are returned to NoCorpusState via a reset or change to the Workspace. A confirmed exit will transition to ClosingState. The events are handled polymorphically through the state_ object, different than having to use conditionals that check state flags. All of the state objects remain on the GUI thread. 
Event Sequence:
Considering the scenario where Reset is pressed during a long corpus build:
1. MainWindow::build_corpus() will submit a workspace copy / snapshot using the function BuildWorker::submit().
2. The worker then processes documents through the function ProcessingCore::rebuild().
3. Using reset will invoke the current state's reset handler function. 
4. BuildWorker::cancel() will increment the counter for generation, and invalidate the pending request. 
5. The active build will check the  cancellation between documents, then stops when the generation becomes outdated. 
6. The GUI will then return to NoCorpusState, and the canceled corpus never gets accepted. 

Section 4 Answers:

Build during a build:
Any additional build requests get ignored while the BuildingState is active. After the cancellation, another build can be requested. 

Remote add: 
Any document added through aiws_send will follow the function add_document_file(), which invalidates the current existing corpus and cancel outdated work. The GUI will then display the updated workspace. 

Shared data: Only the worker control information and the request handoff data get shared. Workspace copies / snapshots and the completed results get transferred without directly sharing the mutable processing objects.  

Fault Switches:
The AIWS_FAULT_NO_CANCEL fault is located in the cancellation callback that lies within BuildWorker::worker_loop(). The cancel_during_build() test is used to detect any unnecessary continued processing.
The AIWS_FAULT_SILENT_SHUTDOWN fault is located in BuildWorker::stop(). The idle-shutdown subprocess test is used to detect the missing wakeup and resulting hang. 

AI Use Disclosure:
ChatGPT was used to create the script and walkthrough for the video. I am currently traveling, so it was used to design the overall structure and flow for the actions taken in the video. 